#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include <clean-core/capped_array.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/detail/incomplete_state_cache.hh>
#include <phantasm-hardware-interface/detail/linked_pool.hh>

#include <phantasm-hardware-interface/d3d12/Fence.hh>
#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>

namespace phi::d3d12
{
/// A single command allocator that keeps track of its lists
/// Unsynchronized
struct cmd_allocator_node
{
public:
    void initialize(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type, int max_num_cmdlists);
    void destroy();

public:
    // API for initial creation of commandlists

    [[nodiscard]] int get_max_num_cmdlists() const { return _max_num_in_flight; }
    [[nodiscard]] ID3D12CommandAllocator* get_allocator() const { return _allocator; }

public:
    // API for normal use

    /// returns true if this node is full
    [[nodiscard]] bool is_full() const { return _full_and_waiting; }

    /// returns true if this node is full and capable of resetting
    [[nodiscard]] bool can_reset() const { return _full_and_waiting && is_submit_counter_up_to_date(); }

    /// acquire memory from this allocator for the given commandlist
    /// do not call if full (best case: blocking, worst case: crash)
    void acquire(ID3D12GraphicsCommandList* cmd_list);

    /// non-blocking reset attempt
    /// returns true if the allocator is usable afterwards
    [[nodiscard]] bool try_reset();

    /// blocking reset attempt
    /// returns true if the allocator is usable afterwards
    [[nodiscard]] bool try_reset_blocking();

public:
    // events

    /// to be called when a command list backed by this allocator
    /// is being submitted
    /// free-threaded
    void on_submit(ID3D12CommandQueue& queue)
    {
        // NOTE: Fence access requires no synchronization in d3d12
        _fence.signalGPU(_submit_counter.fetch_add(1) + 1, queue);
    }

    /// to be called when a command list backed by this allocator
    /// is being discarded (will never result in a submit)
    /// free-threaded
    void on_discard() { _num_discarded.fetch_add(1); }

private:
    /// perform the internal reset
    void do_reset();

    /// returns true if all in-flight cmdlists have been either submitted or discarded
    bool is_submit_counter_up_to_date() const;

private:
    ID3D12CommandAllocator* _allocator;
    SimpleFence _fence;
    std::atomic<uint64_t> _submit_counter = 0;
    uint64_t _submit_counter_at_last_reset = 0;
    int _num_in_flight = 0;
    std::atomic<int> _num_discarded = 0;
    int _max_num_in_flight = 0;
    bool _full_and_waiting = false;
};

/// A bundle of single command allocators which automatically
/// circles through them and soft-resets when possible
/// Unsynchronized
class CommandAllocatorBundle
{
public:
    void initialize(ID3D12Device& device, int num_allocators, int num_cmdlists_per_allocator, cc::span<ID3D12GraphicsCommandList*> initial_lists);
    void destroy();

    /// Resets the given command list to use memory by an appropriate allocator
    /// Returns a pointer to the backing allocator node
    cmd_allocator_node* acquireMemory(ID3D12GraphicsCommandList* list);

private:
    void updateActiveIndex();

private:
    cc::array<cmd_allocator_node> mAllocators;
    size_t mActiveAllocator = 0u;
};

class BackendD3D12;

/// The high-level allocator for Command Lists
/// Synchronized
class CommandListPool
{
public:
    // frontend-facing API (not quite, command_list can only be compiled immediately)

    [[nodiscard]] handle::command_list create(ID3D12GraphicsCommandList*& out_cmdlist,
                                              ID3D12GraphicsCommandList5** out_cmdlist5,
                                              CommandAllocatorBundle& thread_allocator,
                                              ID3D12Fence* fence_to_set = nullptr);

    void freeOnSubmit(handle::command_list cl, ID3D12CommandQueue& queue)
    {
        cmd_list_node& freed_node = mPool.get(static_cast<unsigned>(cl.index));
        {
            auto lg = std::lock_guard(mMutex);
            freed_node.responsible_allocator->on_submit(queue);

            if (freed_node.fence_to_set != nullptr)
            {
                queue.Signal(freed_node.fence_to_set, 1);
            }

            mPool.release(static_cast<unsigned>(cl.index));
        }
    }

    void freeOnSubmit(cc::span<handle::command_list const> cls, ID3D12CommandQueue& queue)
    {
        auto lg = std::lock_guard(mMutex);
        for (auto const& cl : cls)
        {
            if (!cl.is_valid())
                continue;

            cmd_list_node& freed_node = mPool.get(static_cast<unsigned>(cl.index));
            freed_node.responsible_allocator->on_submit(queue);

            if (freed_node.fence_to_set != nullptr)
            {
                queue.Signal(freed_node.fence_to_set, 1);
            }

            mPool.release(static_cast<unsigned>(cl.index));
        }
    }

    void freeOnDiscard(cc::span<handle::command_list const> cls)
    {
        auto lg = std::lock_guard(mMutex);
        for (auto cl : cls)
        {
            if (cl.is_valid())
            {
                mPool.get(static_cast<unsigned>(cl.index)).responsible_allocator->on_discard();
                mPool.release(static_cast<unsigned>(cl.index));
            }
        }
    }

public:
    ID3D12GraphicsCommandList* getRawList(handle::command_list cl) const { return mRawLists[static_cast<unsigned>(cl.index)]; }
    ID3D12GraphicsCommandList5* getRawList5(handle::command_list cl) const { return mRawLists5[static_cast<unsigned>(cl.index)]; }

    phi::detail::incomplete_state_cache* getStateCache(handle::command_list cl)
    {
        return &mPool.get(static_cast<unsigned>(cl.index)).state_cache;
    }

public:
    void initialize(BackendD3D12& backend, int num_allocators_per_thread, int num_cmdlists_per_allocator, cc::span<CommandAllocatorBundle*> thread_allocators);
    void destroy();

private:
    void queryList5();

private:
    struct cmd_list_node
    {
        // an allocated node is always in the following state:
        // - the command list is freshly reset using an appropriate allocator
        // - the responsible_allocator must be informed on submit or discard
        cmd_allocator_node* responsible_allocator;
        ID3D12Fence* fence_to_set; ///< the fence to signal to 1 directly after submission, can be nullptr (not related to internal sync, for handle::event API)
        phi::detail::incomplete_state_cache state_cache;
    };

    /// the pool itself, managing handle association as well as additional
    /// bookkeeping data structures
    phi::detail::linked_pool<cmd_list_node, unsigned> mPool;

    /// a parallel array to the pool, identically indexed
    /// the cmdlists must stay alive even while "unallocated"
    cc::array<ID3D12GraphicsCommandList*> mRawLists;
    cc::array<ID3D12GraphicsCommandList5*> mRawLists5;

    std::mutex mMutex;
    bool mHasLists5 = false;
};
}
