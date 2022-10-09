#include "cmd_list_pool.hh"

#include <clean-core/bit_cast.hh>

#ifdef PHI_HAS_OPTICK
#include <optick.h>
#endif

#include <phantasm-hardware-interface/d3d12/BackendD3D12.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

void phi::d3d12::CommandAllocator::initialize(ID3D12Device5& device, D3D12_COMMAND_LIST_TYPE type)
{
    mSubmitCounter = 0;
    mSubmitCounterAtLastReset = 0;
    mNumBackedCmdlists = 0;
    mNumDiscardedCmdlists = 0;

    mType = type;

    mFence.initialize(device);
    util::set_object_name(mFence.fence, "CommandAllocator fence for %zx", this);

    PHI_D3D12_VERIFY(device.CreateCommandAllocator(type, IID_PPV_ARGS(&mAllocator)));
}

void phi::d3d12::CommandAllocator::destroy()
{
    tryResetStalling(false); // do not warn on destruction

    mAllocator->Release();
    mFence.destroy();
}

void phi::d3d12::CommandAllocator::createCommandLists(ID3D12Device5& device, cc::span<ID3D12GraphicsCommandList5*> out_cmdlists)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT();
#endif

    CC_ASSERT(mAllocator != nullptr && "not initialized");

    char const* const pTypeName = util::to_queue_type_literal(mType);

    for (auto i = 0u; i < out_cmdlists.size(); ++i)
    {
        PHI_D3D12_VERIFY(device.CreateCommandList(0, mType, mAllocator, nullptr, IID_PPV_ARGS(&out_cmdlists[i])));
        out_cmdlists[i]->Close();

        util::set_object_name(out_cmdlists[i], "pooled %s cmdlist #%d", pTypeName, i);
    }
}

void phi::d3d12::CommandAllocator::acquireMemory(ID3D12GraphicsCommandList5* cmd_list)
{
    PHI_D3D12_VERIFY(cmd_list->Reset(mAllocator, nullptr));
    ++mNumBackedCmdlists;
}


// reset the allocator if possible, stalling the CPU if necessary
// returns true if the reset succeeded

inline bool phi::d3d12::CommandAllocator::tryResetStalling(bool bWarnOnStall)
{
    // even for blocking resets, the submit counter must be up to date
    if (mNumBackedCmdlists == 0 || !isSubmitCounterUpToDate())
        return false;

    uint64_t const submitCounterToReach = mSubmitCounter.load();
    bool bDidWait = mFence.waitCPU(submitCounterToReach);

    if (bDidWait && bWarnOnStall)
    {
        PHI_LOG_WARN("Command allocator {} forced to stall CPU on submit #{} ({} cmdlists in flight)", mAllocator, submitCounterToReach, mNumBackedCmdlists);
    }

    resetAllocator();

    return true;
}

void phi::d3d12::CommandAllocator::resetAllocator()
{
    PHI_D3D12_VERIFY(mAllocator->Reset());

    // PHI_LOG_TRACE("Resetted command alloc {} ({} were in flight)", mAllocator, mNumBackedCmdlists);

    mNumBackedCmdlists = 0;
    mNumDiscardedCmdlists = 0;
    mSubmitCounterAtLastReset = mSubmitCounter;
}

bool phi::d3d12::CommandAllocator::isSubmitCounterUpToDate() const
{
    // two atomics are being loaded in this function, mSubmitCounter and _num_discarded
    // both are monotonously increasing, so numSubmitsSinceReset grows, maxNumSubmitsRemaining shrinks
    // as far as i can tell there is no failure mode and the order of the two loads does not matter
    // if numSubmitsSinceReset is loaded early, we assume too few submits (-> return false)
    // if maxNumSubmitsRemaining is loaded early, we assume too many pending lists (-> return false)
    // as the two values can only ever reach equality (and not go past each other), this is safe.
    // this function can only ever prevent resets, never cause them too early
    // once the two values are equal, no further changes will occur to the atomics until the next reset

    // Check if all lists acquired from this allocator since the last reset have been either submitted or discarded
    int const numSubmitsSinceReset = static_cast<int>(mSubmitCounter.load() - mSubmitCounterAtLastReset);
    int const maxNumSubmitsRemaining = mNumBackedCmdlists - mNumDiscardedCmdlists.load();

    CC_ASSERT(numSubmitsSinceReset >= 0 && numSubmitsSinceReset <= maxNumSubmitsRemaining);

    // if this condition is false, there have been less submits than acquired lists (minus the discarded ones)
    // so some are still pending submit (or discardation [sic])
    // we cannot check the fence yet since mSubmitCounter is currently meaningless
    return (numSubmitsSinceReset == maxNumSubmitsRemaining);
}

phi::handle::command_list phi::d3d12::CommandListPool::create(ID3D12GraphicsCommandList5*& out_cmdlist, queue_type type)
{
    handle::command_list hRes;
    cmd_list_node* pNewNode;
    hRes = acquireNodeInternal(type, pNewNode, out_cmdlist);

    auto* const pNewAllocator = mQueue.acquireAllocator(type);
    pNewAllocator->acquireMemory(out_cmdlist);

    pNewNode->bIsLive = true;
    pNewNode->pResponsibleAllocator = pNewAllocator;

    return hRes;
}

void phi::d3d12::CommandListPool::onClose(handle::command_list hList)
{
    cmdlist_linked_pool_t* pPool;
    ID3D12GraphicsCommandList5* pList;
    cmd_list_node* const pNode = getNodeInternal(hList, pPool, pList);

    CC_ASSERT(pNode->bIsLive && "Node is expected to be live when closing");
    mQueue.releaseAllocator(pNode->pResponsibleAllocator, HandleToQueueType(hList));

    pNode->bIsLive = false;
}

void phi::d3d12::CommandListPool::freeOnSubmit(phi::handle::command_list hList, ID3D12CommandQueue& queue)
{
    cmdlist_linked_pool_t* pPool;
    ID3D12GraphicsCommandList5* pList;
    cmd_list_node* const pNode = getNodeInternal(hList, pPool, pList);

    if (pNode->bIsLive)
    {
        mQueue.releaseAllocator(pNode->pResponsibleAllocator, HandleToQueueType(hList));
        pNode->bIsLive = false;
    }

    pNode->pResponsibleAllocator->onListSubmit(queue);
    pPool->unsafe_release_node(pNode);
}

void phi::d3d12::CommandListPool::freeOnSubmit(cc::span<const phi::handle::command_list> spLists, ID3D12CommandQueue& queue)
{
    for (auto const& hList : spLists)
    {
        if (!hList.is_valid())
            continue;

        freeOnSubmit(hList, queue);
    }
}

void phi::d3d12::CommandListPool::freeOnDiscard(cc::span<const phi::handle::command_list> cls)
{
    for (auto hList : cls)
    {
        if (!hList.is_valid())
            continue;

        cmdlist_linked_pool_t* pool;
        ID3D12GraphicsCommandList5* list;
        cmd_list_node* const pNode = getNodeInternal(hList, pool, list);

        if (pNode->bIsLive)
        {
            mQueue.releaseAllocator(pNode->pResponsibleAllocator, HandleToQueueType(hList));
            pNode->bIsLive = false;
        }

        pNode->pResponsibleAllocator->onListDiscard();
        pool->unsafe_release_node(pNode);
    }
}

void phi::d3d12::CommandListPool::initialize(phi::d3d12::BackendD3D12& backend,
                                             cc::allocator* static_alloc,
                                             uint32_t num_direct_allocs,
                                             uint32_t num_direct_lists,
                                             uint32_t num_compute_allocs,
                                             uint32_t num_compute_lists,
                                             uint32_t num_copy_allocs,
                                             uint32_t num_copy_lists,
                                             uint32_t max_num_unique_transitions_per_cmdlist)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT();
#endif

    // initialize data structures
    mPoolDirect.initialize(num_direct_lists, static_alloc);
    mRawListsDirect = mRawListsDirect.uninitialized(num_direct_lists, static_alloc);
    mPoolCompute.initialize(num_compute_lists, static_alloc);
    mRawListsCompute = mRawListsCompute.uninitialized(num_compute_lists, static_alloc);
    mPoolCopy.initialize(num_copy_lists, static_alloc);
    mRawListsCopy = mRawListsCopy.uninitialized(num_copy_lists, static_alloc);

    uint32_t numListsTotal = num_direct_lists + num_compute_lists + num_copy_lists;
    mNumStateCacheEntriesPerCmdlist = max_num_unique_transitions_per_cmdlist;
    mFlatStateCacheEntries = mFlatStateCacheEntries.uninitialized(numListsTotal * max_num_unique_transitions_per_cmdlist, static_alloc);

    mQueue.initialize(static_alloc, num_direct_allocs, num_compute_allocs, num_copy_allocs);

    mAllocatorsDirect.reset(static_alloc, num_direct_allocs);
    mAllocatorsCompute.reset(static_alloc, num_compute_allocs);
    mAllocatorsCopy.reset(static_alloc, num_copy_allocs);
}

void phi::d3d12::CommandListPool::initialize_nth_thread(ID3D12Device5* device, uint32_t thread_idx, uint32_t num_threads)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT("Command List init for Thread");
    OPTICK_TAG("Thread Index", thread_idx);
#endif

    // this parallelizes extremely poorly, do it serially for now
    if (thread_idx != 0)
        return;

    for (auto& alloc : mAllocatorsDirect)
    {
        alloc.initialize(*device, D3D12_COMMAND_LIST_TYPE_DIRECT);
        mQueue.releaseAllocator(&alloc, queue_type::direct);
    }
    for (auto& alloc : mAllocatorsCompute)
    {
        alloc.initialize(*device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
        mQueue.releaseAllocator(&alloc, queue_type::compute);
    }
    for (auto& alloc : mAllocatorsCopy)
    {
        alloc.initialize(*device, D3D12_COMMAND_LIST_TYPE_COPY);
        mQueue.releaseAllocator(&alloc, queue_type::copy);
    }

    mAllocatorsDirect.begin()->createCommandLists(*device, mRawListsDirect);
    mAllocatorsCompute.begin()->createCommandLists(*device, mRawListsCompute);
    mAllocatorsCopy.begin()->createCommandLists(*device, mRawListsCopy);
}

void phi::d3d12::CommandListPool::destroy()
{
    for (auto const list : mRawListsDirect)
        list->Release();

    for (auto const list : mRawListsCompute)
        list->Release();

    for (auto const list : mRawListsCopy)
        list->Release();

    for (auto& alloc : mAllocatorsDirect)
        alloc.destroy();

    for (auto& alloc : mAllocatorsCompute)
        alloc.destroy();

    for (auto& alloc : mAllocatorsCopy)
        alloc.destroy();
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
