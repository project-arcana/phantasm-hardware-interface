#include "cmd_list_pool.hh"

#include <phantasm-hardware-interface/d3d12/BackendD3D12.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

void phi::d3d12::cmd_allocator_node::initialize(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type, int max_num_cmdlists)
{
    _max_num_in_flight = max_num_cmdlists;
    _submit_counter = 0;
    _submit_counter_at_last_reset = 0;
    _num_in_flight = 0;
    _num_discarded = 0;
    _full_and_waiting = false;

    _fence.initialize(device);
    util::set_object_name(_fence.getRawFence(), "cmd_allocator_node fence for %zx", this);

    PHI_D3D12_VERIFY(device.CreateCommandAllocator(type, IID_PPV_ARGS(&_allocator)));
}

void phi::d3d12::cmd_allocator_node::destroy() { _allocator->Release(); }

void phi::d3d12::cmd_allocator_node::acquire(ID3D12GraphicsCommandList* cmd_list)
{
    if (is_full())
    {
        // the allocator is full, we are almost dead but might be able to reset
        auto const reset_success = try_reset_blocking();
        CC_RUNTIME_ASSERT(reset_success && "cmdlist allocator node overcommitted and unable to recover");
        // we were able to recover, but should warn even now
    }

    PHI_D3D12_VERIFY(cmd_list->Reset(_allocator, nullptr));
    ++_num_in_flight;

    if (_num_in_flight == _max_num_in_flight)
        _full_and_waiting = true;
}

bool phi::d3d12::cmd_allocator_node::try_reset()
{
    if (can_reset())
    {
        // full, and all acquired cmdlists have been either submitted or discarded, check the fence

        auto const fence_current = _fence.getCurrentValue();
        CC_ASSERT(fence_current <= _submit_counter && "counter overflow (after > 4*10^8 years)");
        if (fence_current == _submit_counter)
        {
            // can reset, and the fence has reached its goal
            do_reset();
            return true;
        }
        else
        {
            // can reset, but the fence hasn't reached its goal yet
            return false;
        }
    }
    else
    {
        // can't reset
        return !is_full();
    }
}

bool phi::d3d12::cmd_allocator_node::try_reset_blocking()
{
    if (can_reset())
    {
        // full, and all acquired cmdlists have been either submitted or discarded, wait for the fence
        _fence.waitCPU(_submit_counter);
        do_reset();
        return true;
    }
    else
    {
        // can't reset
        return !is_full();
    }
}

void phi::d3d12::cmd_allocator_node::do_reset()
{
    PHI_D3D12_VERIFY(_allocator->Reset());
    _full_and_waiting = false;
    _num_in_flight = 0;
    _num_discarded = 0;
    _submit_counter_at_last_reset = _submit_counter;
}

bool phi::d3d12::cmd_allocator_node::is_submit_counter_up_to_date() const
{
    // two atomics are being loaded in this function, _submit_counter and _num_discarded
    // both are monotonously increasing, so submits_since_reset grows, possible_submits_remaining shrinks
    // as far as i can tell there is no failure mode and the order of the two loads does not matter
    // if submits_since_reset is loaded early, we assume too few submits (-> return false)
    // if possible_submits_remaining is loaded early, we assume too many pending lists (-> return false)
    // as the two values can only ever reach equality (and not go past each other), this is safe.
    // this function can only ever prevent resets, never cause them too early
    // once the two values are equal, no further changes will occur to the atomics until the next reset

    // Check if all lists acquired from this allocator
    // since the last reset have been either submitted or discarded
    int const submits_since_reset = static_cast<int>(_submit_counter.load() - _submit_counter_at_last_reset);
    int const possible_submits_remaining = _num_in_flight - _num_discarded.load();

    // this assert is paranoia-tier
    CC_ASSERT(submits_since_reset >= 0 && submits_since_reset <= possible_submits_remaining);

    // if this condition is false, there have been less submits than acquired lists (minus the discarded ones)
    // so some are still pending submit (or discardation [sic])
    // we cannot check the fence yet since _submit_counter is currently meaningless
    return (submits_since_reset == possible_submits_remaining);
}

phi::handle::command_list phi::d3d12::CommandListPool::create(ID3D12GraphicsCommandList*& out_cmdlist,
                                                                              ID3D12GraphicsCommandList5** out_cmdlist5,
                                                                              CommandAllocatorBundle& thread_allocator,
                                                                              ID3D12Fence* fence_to_set)
{
    unsigned res_handle;
    {
        auto lg = std::lock_guard(mMutex);
        res_handle = mPool.acquire();
    }

    out_cmdlist = mRawLists[res_handle];

    if (out_cmdlist5)
    {
        *out_cmdlist5 = mRawLists5[res_handle];
    }

    cmd_list_node& new_node = mPool.get(res_handle);
    new_node.responsible_allocator = thread_allocator.acquireMemory(out_cmdlist);
    new_node.fence_to_set = fence_to_set;

    return {static_cast<handle::index_t>(res_handle)};
}

void phi::d3d12::CommandListPool::initialize(phi::d3d12::BackendD3D12& backend,
                                                     int num_allocators_per_thread,
                                                     int num_cmdlists_per_allocator,
                                                     cc::span<CommandAllocatorBundle*> thread_allocators)
{
    auto const num_cmdlists_per_thread = static_cast<size_t>(num_allocators_per_thread * num_cmdlists_per_allocator);
    auto const num_cmdlists_total = num_cmdlists_per_thread * thread_allocators.size();

    // initialize data structures
    mPool.initialize(num_cmdlists_total);
    mRawLists = mRawLists.uninitialized(num_cmdlists_total);

    // initialize the (currently single) allocator bundle and give it ALL raw lists
    for (auto i = 0u; i < thread_allocators.size(); ++i)
    {
        thread_allocators[i]->initialize(backend.getDevice(), num_allocators_per_thread, num_cmdlists_per_allocator,
                                         cc::span{mRawLists.data() + (num_cmdlists_per_thread * i), num_cmdlists_per_thread});
    }

    // Execute all (closed) lists once to suppress warnings
    static_assert(std::is_same_v<decltype(mRawLists[0]), ID3D12GraphicsCommandList*&>, "Elements of mRawLists not valid for cast");
    backend.getDirectQueue().ExecuteCommandLists(                     //
        UINT(mRawLists.size()),                                       //
        reinterpret_cast<ID3D12CommandList* const*>(mRawLists.data()) // This is hopefully always fine
    );

    // Query GraphicsCommandList5
    queryList5();

    // Flush the backend
    backend.flushGPU();
}

void phi::d3d12::CommandListPool::destroy()
{
    if (mHasLists5)
    {
        for (auto const list5 : mRawLists5)
        {
            list5->Release();
        }
    }

    for (auto const list : mRawLists)
    {
        list->Release();
    }
}

void phi::d3d12::CommandListPool::queryList5()
{
    mHasLists5 = true;
    mRawLists5 = mRawLists5.uninitialized(mRawLists.size());
    for (auto i = 0u; i < mRawLists.size(); ++i)
    {
        auto const success = SUCCEEDED(mRawLists[i]->QueryInterface(IID_PPV_ARGS(&mRawLists5[i])));

        if (!success)
        {
            mHasLists5 = false;
            break;
        }
    }

    if (!mHasLists5)
    {
        cc::fill(mRawLists5, nullptr);
    }
}

void phi::d3d12::CommandAllocatorBundle::initialize(ID3D12Device& device, int num_allocators, int num_cmdlists_per_allocator, cc::span<ID3D12GraphicsCommandList*> initial_lists)
{
    // NOTE: Eventually, we'll need multiple
    constexpr auto list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    mAllocators = mAllocators.defaulted(static_cast<size_t>(num_allocators));
    mActiveAllocator = 0u;


    // Initialize allocators, create command lists
    {
        auto cmdlist_i = 0u;
        for (cmd_allocator_node& alloc_node : mAllocators)
        {
            alloc_node.initialize(device, list_type, num_cmdlists_per_allocator);
            ID3D12CommandAllocator* const raw_alloc = alloc_node.get_allocator();

            for (auto _ = 0; _ < num_cmdlists_per_allocator; ++_)
            {
                PHI_D3D12_VERIFY(device.CreateCommandList(0, list_type, raw_alloc, nullptr, IID_PPV_ARGS(&initial_lists[cmdlist_i])));
                initial_lists[cmdlist_i]->Close();
                util::set_object_name(initial_lists[cmdlist_i], "pool cmdlist #%d, alloc_bundle %zx", cmdlist_i, this);
                ++cmdlist_i;
            }
        }
    }
}

void phi::d3d12::CommandAllocatorBundle::destroy()
{
    for (cmd_allocator_node& alloc_node : mAllocators)
    {
        auto const reset_success = alloc_node.try_reset_blocking();
        CC_RUNTIME_ASSERT(reset_success);
        alloc_node.destroy();
    }
}

phi::d3d12::cmd_allocator_node* phi::d3d12::CommandAllocatorBundle::acquireMemory(ID3D12GraphicsCommandList* list)
{
    updateActiveIndex();
    mAllocators[mActiveAllocator].acquire(list);
    return &mAllocators[mActiveAllocator];
}

void phi::d3d12::CommandAllocatorBundle::updateActiveIndex()
{
    for (auto it = 0u; it < mAllocators.size(); ++it)
    {
        if (!mAllocators[mActiveAllocator].is_full() || mAllocators[mActiveAllocator].try_reset())
            // not full, or nonblocking reset successful
            return;
        else
        {
            ++mActiveAllocator;
            // fast %
            if (mActiveAllocator >= mAllocators.size())
                mActiveAllocator -= mAllocators.size();
        }
    }

    // all non-blocking resets failed, try blocking now
    for (auto it = 0u; it < mAllocators.size(); ++it)
    {
        if (mAllocators[mActiveAllocator].try_reset_blocking())
            // blocking reset successful
            return;
        else
        {
            ++mActiveAllocator;
            // fast %
            if (mActiveAllocator >= mAllocators.size())
                mActiveAllocator -= mAllocators.size();
        }
    }

    // all allocators have at least 1 dangling cmdlist, we cannot recover
    CC_RUNTIME_ASSERT(false && "all allocators overcommitted and unresettable");
}
