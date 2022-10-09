#pragma once

#include <atomic>
#include <cstdint>

#include <clean-core/alloc_array.hh>
#include <clean-core/atomic_linked_pool.hh>
#include <clean-core/bits.hh>
#include <clean-core/experimental/mpmc_queue.hh>

#include <phantasm-hardware-interface/arguments.hh>

#include <phantasm-hardware-interface/d3d12/Fence.hh>
#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/d3d12/common/incomplete_state_cache.hh>
#include <phantasm-hardware-interface/d3d12/fwd.hh>

namespace phi::d3d12
{
// A single command allocator that keeps track of its lists
// Unsynchronized - N per CommandAllocatorBundle
struct CommandAllocator
{
public:
    void initialize(ID3D12Device5& device, D3D12_COMMAND_LIST_TYPE type);
    void destroy();

    void createCommandLists(ID3D12Device5& device, cc::span<ID3D12GraphicsCommandList5*> out_cmdlists);

public:
    // acquire memory from this allocator for the given commandlist
    // do not call if full (best case: blocking, worst case: crash)
    void acquireMemory(ID3D12GraphicsCommandList5* cmd_list);

    bool canReset() const
    {
        return mNumBackedCmdlists > 0 &&                          // 1. any command list is currently backed
               isSubmitCounterUpToDate() &&                       // 2. all backed command lists are submitted or discarded
               mFence.getCurrentValue() == mSubmitCounter.load(); // 3. the submit counter was GPU-reached
    }

    int getNumBackedCmdlists() const { return mNumBackedCmdlists; }

    // reset the allocator without stalling if possible
    // returns true if the reset succeeded
    bool tryResetFast()
    {
        if (!canReset())
            return false;

        resetAllocator();
        return true;
    }

    // reset the allocator if possible, stalling the CPU if necessary
    // returns true if the reset succeeded
    bool tryResetStalling(bool bWarnOnStall);

public:
    // events

    // to be called when a command list backed by this allocator
    // is being submitted
    // free-threaded
    void onListSubmit(ID3D12CommandQueue& queue)
    {
        // NOTE: Fence access requires no synchronization in d3d12
        auto const submitCountPrev = mSubmitCounter.fetch_add(1);
        mFence.signalGPU(submitCountPrev + 1, queue);
    }

    // to be called when a command list backed by this allocator
    // is being discarded (will never result in a submit)
    // free-threaded
    void onListDiscard() { mNumDiscardedCmdlists.fetch_add(1); }

private:
    // perform the internal reset
    void resetAllocator();

    // returns true if all in-flight cmdlists have been either submitted or discarded
    bool isSubmitCounterUpToDate() const;

private:
    ID3D12CommandAllocator* mAllocator = nullptr;
    D3D12_COMMAND_LIST_TYPE mType;
    SimpleFence mFence;

    // +1 on each submit (never reset)
    std::atomic<uint64_t> mSubmitCounter = 0;
    uint64_t mSubmitCounterAtLastReset = 0;

    // amount of backed command lists since reset
    int mNumBackedCmdlists = 0;

    // amount of backed discard command lists since reset
    std::atomic<int> mNumDiscardedCmdlists = 0;
};

struct CommandAllocatorQueue
{
    cc::mpmc_queue<CommandAllocator*> queueDirect;
    cc::mpmc_queue<CommandAllocator*> queueCompute;
    cc::mpmc_queue<CommandAllocator*> queueCopy;

    // amount of lists after which allocators are fast-resetted if possible
    uint32_t listLimitFastReset = 10;
    // amount of lists after which allocators are resetted with stalling
    uint32_t listLimitStallingReset = 25;

    void initialize(cc::allocator* pStaticAlloc, uint32_t numDirect, uint32_t numCompute, uint32_t numCopy)
    {
        queueDirect.initialize(cc::ceil_pow2(numDirect), pStaticAlloc);
        queueCompute.initialize(cc::ceil_pow2(numCompute), pStaticAlloc);
        queueCopy.initialize(cc::ceil_pow2(numCopy), pStaticAlloc);
    }

    CommandAllocator* acquireAllocator(queue_type type)
    {
        CommandAllocator* pRes = nullptr;
        bool bSuccess = false;

        switch (type)
        {
        case queue_type::direct:
            bSuccess = queueDirect.dequeue(&pRes);
            break;
        case queue_type::compute:
            bSuccess = queueCompute.dequeue(&pRes);
            break;
        case queue_type::copy:
            bSuccess = queueCopy.dequeue(&pRes);
            break;
        }


        CC_RUNTIME_ASSERT(bSuccess && "No command allocator available, too many live command lists at once");
        CC_ASSERT(listLimitFastReset > 0 && listLimitFastReset <= listLimitStallingReset);

        auto const numBackedCmdlists = pRes->getNumBackedCmdlists();

        if (numBackedCmdlists >= listLimitStallingReset)
        {
            pRes->tryResetStalling(true);
        }
        else if (numBackedCmdlists >= listLimitFastReset)
        {
            pRes->tryResetFast();
        }

        return pRes;
    }

    void releaseAllocator(CommandAllocator* pAllocator, queue_type type)
    {
        bool bSuccess = false;

        switch (type)
        {
        case queue_type::direct:
            bSuccess = queueDirect.enqueue(pAllocator);
            break;
        case queue_type::compute:
            bSuccess = queueCompute.enqueue(pAllocator);
            break;
        case queue_type::copy:
            bSuccess = queueCopy.enqueue(pAllocator);
            break;
        }

        CC_ASSERT(bSuccess && "Double-released command allocator");
    }
};

// The high-level allocator for Command Lists
// Synchronized - 1 per application
class CommandListPool
{
private:
    struct cmd_list_node
    {
        // an allocated node is always in the following state:
        // - the command list is freshly reset using an appropriate allocator
        // - the pResponsibleAllocator must be informed on submit or discard
        bool bIsLive = false;
        CommandAllocator* pResponsibleAllocator = nullptr;
        incomplete_state_cache state_cache;
    };

    using cmdlist_linked_pool_t = cc::atomic_linked_pool<cmd_list_node>;

    static queue_type HandleToQueueType(handle::command_list cl)
    {
        int const num_leading_zeroes = cc::count_leading_zeros(cl._value);
        CC_ASSERT(num_leading_zeroes <= 2 && "invalid commandlist handle"); // one of the three MSBs must be set
        return queue_type(num_leading_zeroes);
    }

    static constexpr handle::command_list AddHandlePaddingFlags(uint32_t pool_handle, queue_type type)
    {
        // we rely on underlying values here
        static_assert(int(queue_type::direct) == 0, "unexpected enum ordering");
        static_assert(int(queue_type::compute) == 1, "unexpected enum ordering");
        static_assert(int(queue_type::copy) == 2, "unexpected enum ordering");
        return {pool_handle | uint32_t(1) << (31 - int(type))};
    }

    cmdlist_linked_pool_t& getPool(queue_type type)
    {
        switch (type)
        {
        case queue_type::direct:
            return mPoolDirect;
        case queue_type::compute:
            return mPoolCompute;
        case queue_type::copy:
            return mPoolCopy;
        }

        CC_UNREACHABLE("invalid queue_type");
    }

    cmdlist_linked_pool_t const& getPool(queue_type type) const
    {
        switch (type)
        {
        case queue_type::direct:
            return mPoolDirect;
        case queue_type::compute:
            return mPoolCompute;
        case queue_type::copy:
            return mPoolCopy;
        }

        CC_UNREACHABLE("invalid queue_type");
    }

    uint32_t getFlatIndexOffset(queue_type type) const
    {
        switch (type)
        {
        case queue_type::direct:
            return 0;
        case queue_type::compute:
            return uint32_t(mPoolDirect.max_size());
        case queue_type::copy:
            return uint32_t(mPoolDirect.max_size() + mPoolCompute.max_size());
        }

        CC_UNREACHABLE("invalid queue_type");
    }

    ID3D12GraphicsCommandList5* getList(handle::command_list cl, queue_type type) const
    {
        switch (type)
        {
        case queue_type::direct:
            return mRawListsDirect[mPoolDirect.get_handle_index(cl._value)];
        case queue_type::compute:
            return mRawListsCompute[mPoolCompute.get_handle_index(cl._value)];
        case queue_type::copy:
            return mRawListsCopy[mPoolCopy.get_handle_index(cl._value)];
        }

        CC_UNREACHABLE("invalid queue_type");
    }

public:
    // frontend-facing API (not quite, command_list can only be compiled immediately)

    [[nodiscard]] handle::command_list create(ID3D12GraphicsCommandList5*& out_cmdlist, queue_type type);

    void onClose(handle::command_list hList);

    void freeOnSubmit(handle::command_list hList, ID3D12CommandQueue& queue);

    void freeOnSubmit(cc::span<handle::command_list const> spLists, ID3D12CommandQueue& queue);

    void freeOnDiscard(cc::span<handle::command_list const> spLists);

public:
    ID3D12GraphicsCommandList5* getRawList(handle::command_list hList) const
    {
        auto const type = HandleToQueueType(hList);
        return getList(hList, type);
    }

    incomplete_state_cache* getStateCache(handle::command_list hList) { return &getNodeInternal(hList)->state_cache; }

public:
    void initialize(BackendD3D12& backend,
                    cc::allocator* static_alloc,
                    uint32_t num_direct_allocs,
                    uint32_t num_direct_lists,
                    uint32_t num_compute_allocs,
                    uint32_t num_compute_lists,
                    uint32_t num_copy_allocs,
                    uint32_t num_copy_lists,
                    uint32_t max_num_unique_transitions_per_cmdlist);

    void initialize_nth_thread(ID3D12Device5* device, uint32_t thread_idx, uint32_t num_threads);

    void destroy();

private:
    handle::command_list acquireNodeInternal(queue_type type, cmd_list_node*& out_node, ID3D12GraphicsCommandList5*& out_cmdlist);

    [[nodiscard]] cmd_list_node* getNodeInternal(handle::command_list cl)
    {
        queue_type const type = HandleToQueueType(cl);
        return &getPool(type).get(cl._value);
    }

    cmd_list_node* getNodeInternal(handle::command_list cl, cmdlist_linked_pool_t*& out_pool, ID3D12GraphicsCommandList5*& out_cmdlist)
    {
        queue_type const type = HandleToQueueType(cl);
        out_pool = &getPool(type);
        out_cmdlist = getList(cl, type);
        return &out_pool->get(cl._value);
    }

private:
    // the linked pools per cmdlist type, managing handle association as well as additional
    // bookkeeping data structures
    cmdlist_linked_pool_t mPoolDirect;
    cmdlist_linked_pool_t mPoolCompute;
    cmdlist_linked_pool_t mPoolCopy;

    // flat memory for the state caches
    uint32_t mNumStateCacheEntriesPerCmdlist = 0;
    cc::alloc_array<incomplete_state_cache::cache_entry> mFlatStateCacheEntries;

    // parallel arrays to the pools, identically indexed
    // the cmdlists must stay alive even while "unallocated"
    cc::alloc_array<ID3D12GraphicsCommandList5*> mRawListsDirect;
    cc::alloc_array<ID3D12GraphicsCommandList5*> mRawListsCompute;
    cc::alloc_array<ID3D12GraphicsCommandList5*> mRawListsCopy;

    CommandAllocatorQueue mQueue;
    cc::alloc_array<CommandAllocator> mAllocatorsDirect;
    cc::alloc_array<CommandAllocator> mAllocatorsCompute;
    cc::alloc_array<CommandAllocator> mAllocatorsCopy;
};
} // namespace phi::d3d12
