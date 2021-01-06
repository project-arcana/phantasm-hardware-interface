#include "cmd_list_pool.hh"

#include <clean-core/bit_cast.hh>

#include <phantasm-hardware-interface/d3d12/BackendD3D12.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

void phi::d3d12::cmd_allocator_node::initialize(ID3D12Device5& device, D3D12_COMMAND_LIST_TYPE type, int max_num_cmdlists)
{
    _max_num_in_flight = max_num_cmdlists;
    _submit_counter = 0;
    _submit_counter_at_last_reset = 0;
    _num_in_flight = 0;
    _num_discarded = 0;
    _full_and_waiting = false;

    _fence.initialize(device);
    util::set_object_name(_fence.fence, "cmd_allocator_node fence for %zx", this);

    PHI_D3D12_VERIFY(device.CreateCommandAllocator(type, IID_PPV_ARGS(&_allocator)));
}

void phi::d3d12::cmd_allocator_node::destroy()
{
    _allocator->Release();
    _fence.destroy();
}

void phi::d3d12::cmd_allocator_node::acquire(ID3D12GraphicsCommandList5* cmd_list)
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

phi::handle::command_list phi::d3d12::CommandListPool::create(ID3D12GraphicsCommandList5*& out_cmdlist, CommandAllocatorsPerThread& thread_allocator, queue_type type)
{
    handle::command_list res_handle;
    cmd_list_node* new_node;
    res_handle = acquireNodeInternal(type, new_node, out_cmdlist);


    new_node->responsible_allocator = thread_allocator.get(type).acquireMemory(out_cmdlist);
    return res_handle;
}

void phi::d3d12::CommandListPool::freeOnSubmit(phi::handle::command_list cl, ID3D12CommandQueue& queue)
{
    cmdlist_linked_pool_t* pool;
    ID3D12GraphicsCommandList5* list;
    cmd_list_node* const node = getNodeInternal(cl, pool, list);
    node->responsible_allocator->on_submit(queue);
    pool->unsafe_release_node(node);
}

void phi::d3d12::CommandListPool::freeOnSubmit(cc::span<const phi::handle::command_list> cls, ID3D12CommandQueue& queue)
{
    for (auto const& cl : cls)
    {
        if (!cl.is_valid())
            continue;

        cmdlist_linked_pool_t* pool;
        ID3D12GraphicsCommandList5* list;
        cmd_list_node* const node = getNodeInternal(cl, pool, list);

        node->responsible_allocator->on_submit(queue);
        pool->unsafe_release_node(node);
    }
}

void phi::d3d12::CommandListPool::freeOnDiscard(cc::span<const phi::handle::command_list> cls)
{
    for (auto cl : cls)
    {
        if (cl.is_valid())
        {
            cmdlist_linked_pool_t* pool;
            ID3D12GraphicsCommandList5* list;
            cmd_list_node* const node = getNodeInternal(cl, pool, list);

            node->responsible_allocator->on_discard();
            pool->unsafe_release_node(node);
        }
    }
}

void phi::d3d12::CommandListPool::initialize(phi::d3d12::BackendD3D12& backend,
                                             cc::allocator* static_alloc,
                                             int num_direct_allocs,
                                             int num_direct_lists_per_alloc,
                                             int num_compute_allocs,
                                             int num_compute_lists_per_alloc,
                                             int num_copy_allocs,
                                             int num_copy_lists_per_alloc,
                                             int max_num_unique_transitions_per_cmdlist,
                                             cc::span<CommandAllocatorsPerThread*> thread_allocators)
{
    auto const num_direct_lists_per_thread = size_t(num_direct_allocs * num_direct_lists_per_alloc);
    auto const num_compute_lists_per_thread = size_t(num_compute_allocs * num_compute_lists_per_alloc);
    auto const num_copy_lists_per_thread = size_t(num_copy_allocs * num_copy_lists_per_alloc);

    auto const num_direct_lists_total = num_direct_lists_per_thread * thread_allocators.size();
    auto const num_compute_lists_total = num_compute_lists_per_thread * thread_allocators.size();
    auto const num_copy_lists_total = num_copy_lists_per_thread * thread_allocators.size();

    auto const num_lists_total = num_direct_lists_total + num_compute_lists_total + num_copy_lists_total;

    // initialize data structures
    mPoolDirect.initialize(num_direct_lists_total, static_alloc);
    mRawListsDirect = mRawListsDirect.uninitialized(num_direct_lists_total, static_alloc);
    mPoolCompute.initialize(num_compute_lists_total, static_alloc);
    mRawListsCompute = mRawListsCompute.uninitialized(num_compute_lists_total, static_alloc);
    mPoolCopy.initialize(num_copy_lists_total, static_alloc);
    mRawListsCopy = mRawListsCopy.uninitialized(num_copy_lists_total, static_alloc);

    mNumStateCacheEntriesPerCmdlist = max_num_unique_transitions_per_cmdlist;
    mFlatStateCacheEntries = mFlatStateCacheEntries.uninitialized(num_lists_total * max_num_unique_transitions_per_cmdlist, static_alloc);

    // initialize the three allocator bundles (direct, compute, copy)
    for (auto i = 0u; i < thread_allocators.size(); ++i)
    {
        thread_allocators[i]->bundle_direct.initialize(*backend.nativeGetDevice(), static_alloc, D3D12_COMMAND_LIST_TYPE_DIRECT, num_direct_allocs,
                                                       num_direct_lists_per_alloc,
                                                       cc::span{mRawListsDirect}.subspan(i * num_direct_lists_per_thread, num_direct_lists_per_thread));

        thread_allocators[i]->bundle_compute.initialize(
            *backend.nativeGetDevice(), static_alloc, D3D12_COMMAND_LIST_TYPE_COMPUTE, num_compute_allocs, num_compute_lists_per_alloc,
            cc::span{mRawListsCompute}.subspan(i * num_compute_lists_per_thread, num_compute_lists_per_thread));

        thread_allocators[i]->bundle_copy.initialize(*backend.nativeGetDevice(), static_alloc, D3D12_COMMAND_LIST_TYPE_COPY, num_copy_allocs, num_copy_lists_per_alloc,
                                                     cc::span{mRawListsCopy}.subspan(i * num_copy_lists_per_thread, num_copy_lists_per_thread));
    }
}

void phi::d3d12::CommandListPool::destroy()
{
    for (auto const list : mRawListsDirect)
        list->Release();

    for (auto const list : mRawListsCompute)
        list->Release();

    for (auto const list : mRawListsCopy)
        list->Release();
}

phi::handle::command_list phi::d3d12::CommandListPool::acquireNodeInternal(phi::queue_type type,
                                                                           phi::d3d12::CommandListPool::cmd_list_node*& out_node,
                                                                           ID3D12GraphicsCommandList5*& out_cmdlist)
{
    auto& pool = getPool(type);
    unsigned const res = pool.acquire();

    unsigned const res_flat_index = pool.get_handle_index(res) + getFlatIndexOffset(type);

    out_node = &pool.get(res);
    out_node->state_cache.initialize(cc::span(mFlatStateCacheEntries).subspan(res_flat_index * mNumStateCacheEntriesPerCmdlist, mNumStateCacheEntriesPerCmdlist));

    auto const res_with_padding_flags = AddHandlePaddingFlags(res, type);
    out_cmdlist = getList(res_with_padding_flags, type);
    return res_with_padding_flags;
}

void phi::d3d12::CommandAllocatorBundle::initialize(ID3D12Device5& device,
                                                    cc::allocator* static_alloc,
                                                    D3D12_COMMAND_LIST_TYPE type,
                                                    int num_allocs,
                                                    int num_lists_per_alloc,
                                                    cc::span<ID3D12GraphicsCommandList5*> out_list)
{
    mAllocators = mAllocators.defaulted(size_t(num_allocs), static_alloc);

    // Initialize allocators, create command lists

    auto cmdlist_i = 0u;

    for (cmd_allocator_node& alloc_node : mAllocators)
    {
        internalInit(device, alloc_node, type, num_lists_per_alloc, out_list.subspan(cmdlist_i, num_lists_per_alloc));
        cmdlist_i += num_lists_per_alloc;
    }
}

void phi::d3d12::CommandAllocatorBundle::destroy()
{
    for (cmd_allocator_node& node : mAllocators)
        internalDestroy(node);
}

phi::d3d12::cmd_allocator_node* phi::d3d12::CommandAllocatorBundle::acquireMemory(ID3D12GraphicsCommandList5* list)
{
    updateActiveIndex();
    mAllocators[mActiveIndex].acquire(list);
    return &mAllocators[mActiveIndex];
}

void phi::d3d12::CommandAllocatorBundle::updateActiveIndex()
{
    for (auto it = 0u; it < mAllocators.size(); ++it)
    {
        if (!mAllocators[mActiveIndex].is_full() || mAllocators[mActiveIndex].try_reset())
            // not full, or nonblocking reset successful
            return;
        else
        {
            mActiveIndex = cc::wrapped_increment(mActiveIndex, mAllocators.size());
        }
    }

    // all non-blocking resets failed, try blocking now
    for (auto it = 0u; it < mAllocators.size(); ++it)
    {
        if (mAllocators[mActiveIndex].try_reset_blocking())
            // blocking reset successful
            return;
        else
        {
            mActiveIndex = cc::wrapped_increment(mActiveIndex, mAllocators.size());
        }
    }

    // all allocators have at least 1 dangling cmdlist, we cannot recover
    CC_RUNTIME_ASSERT(false && "all allocators overcommitted and unresettable");
}

void phi::d3d12::CommandAllocatorBundle::internalDestroy(phi::d3d12::cmd_allocator_node& node)
{
    auto const reset_success = node.try_reset_blocking();
    CC_RUNTIME_ASSERT(reset_success);
    node.destroy();
}

void phi::d3d12::CommandAllocatorBundle::internalInit(ID3D12Device5& device,
                                                      phi::d3d12::cmd_allocator_node& node,
                                                      D3D12_COMMAND_LIST_TYPE list_type,
                                                      unsigned num_cmdlists,
                                                      cc::span<ID3D12GraphicsCommandList5*> out_cmdlists)
{
    node.initialize(device, list_type, num_cmdlists);
    ID3D12CommandAllocator* const raw_alloc = node.get_allocator();

    char const* const queuetype_literal = util::to_queue_type_literal(list_type);

    for (auto i = 0u; i < num_cmdlists; ++i)
    {
        PHI_D3D12_VERIFY(device.CreateCommandList(0, list_type, raw_alloc, nullptr, IID_PPV_ARGS(&out_cmdlists[i])));
        out_cmdlists[i]->Close();
        util::set_object_name(out_cmdlists[i], "pooled %s cmdlist #%d, alloc_bundle %p", queuetype_literal, i, this);
    }
}
