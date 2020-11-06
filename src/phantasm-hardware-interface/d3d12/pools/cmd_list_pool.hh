#pragma once

#include <atomic>
#include <cstdint>

#include <clean-core/array.hh>
#include <clean-core/bits.hh>
#include <clean-core/capped_array.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/common/container/linked_pool.hh>
#include <phantasm-hardware-interface/common/incomplete_state_cache.hh>

#include <phantasm-hardware-interface/d3d12/Fence.hh>
#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/d3d12/fwd.hh>

namespace phi::d3d12
{
/// A single command allocator that keeps track of its lists
/// Unsynchronized - N per CommandAllocatorBundle
struct cmd_allocator_node
{
public:
    void initialize(ID3D12Device5& device, D3D12_COMMAND_LIST_TYPE type, int max_num_cmdlists);
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
    void acquire(ID3D12GraphicsCommandList5* cmd_list);

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
/// Unsynchronized - 1 per thread, per queue type
class CommandAllocatorBundle
{
public:
    void initialize(ID3D12Device5& device, D3D12_COMMAND_LIST_TYPE type, int num_allocs, int num_lists_per_alloc, cc::span<ID3D12GraphicsCommandList5*> out_list);
    void destroy();

    /// Resets the given command list to use memory by an appropriate allocator
    /// Returns a pointer to the backing allocator node
    cmd_allocator_node* acquireMemory(ID3D12GraphicsCommandList5* list);

    void updateActiveIndex();

private:
    void internalDestroy(cmd_allocator_node& node);
    void internalInit(ID3D12Device5& device, cmd_allocator_node& node, D3D12_COMMAND_LIST_TYPE list_type, unsigned num_cmdlists, cc::span<ID3D12GraphicsCommandList5*> out_cmdlists);

private:
    cc::array<cmd_allocator_node> mAllocators;
    size_t mActiveIndex = 0u;
};

struct CommandAllocatorsPerThread
{
    CommandAllocatorBundle bundle_direct;
    CommandAllocatorBundle bundle_compute;
    CommandAllocatorBundle bundle_copy;

    void destroy()
    {
        bundle_direct.destroy();
        bundle_compute.destroy();
        bundle_copy.destroy();
    }

    CommandAllocatorBundle& get(queue_type type)
    {
        switch (type)
        {
        case queue_type::direct:
            return bundle_direct;
        case queue_type::compute:
            return bundle_compute;
        case queue_type::copy:
            return bundle_copy;
        }

        CC_UNREACHABLE("invalid queue type");
        return bundle_direct;
    }
};

/// The high-level allocator for Command Lists
/// Synchronized - 1 per application
class CommandListPool
{
private:
    struct cmd_list_node
    {
        // an allocated node is always in the following state:
        // - the command list is freshly reset using an appropriate allocator
        // - the responsible_allocator must be informed on submit or discard
        cmd_allocator_node* responsible_allocator;
        d3d12_incomplete_state_cache state_cache;
    };

    using cmdlist_linked_pool_t = phi::linked_pool<cmd_list_node>;

    static queue_type HandleToQueueType(handle::command_list cl)
    {
        int const num_leading_zeroes = cc::count_leading_zeros(cl._value);
        CC_ASSERT(num_leading_zeroes <= 2 && "invalid commandlist handle"); // one of the three MSBs must be set
        return queue_type(num_leading_zeroes);
    }

    static constexpr handle::command_list IndexToHandle(uint32_t pool_handle, queue_type type)
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

    [[nodiscard]] handle::command_list create(ID3D12GraphicsCommandList5*& out_cmdlist, CommandAllocatorsPerThread& thread_allocator, queue_type type);

    void freeOnSubmit(handle::command_list cl, ID3D12CommandQueue& queue);

    void freeOnSubmit(cc::span<handle::command_list const> cls, ID3D12CommandQueue& queue);

    void freeOnDiscard(cc::span<handle::command_list const> cls);

public:
    ID3D12GraphicsCommandList5* getRawList(handle::command_list cl)
    {
        auto const type = HandleToQueueType(cl);
        return getList(cl, type);
    }

    d3d12_incomplete_state_cache* getStateCache(handle::command_list cl) { return &getNodeInternal(cl)->state_cache; }

public:
    void initialize(BackendD3D12& backend,
                    int num_direct_allocs,
                    int num_direct_lists_per_alloc,
                    int num_compute_allocs,
                    int num_compute_lists_per_alloc,
                    int num_copy_allocs,
                    int num_copy_lists_per_alloc,
                    cc::span<CommandAllocatorsPerThread*> thread_allocators);
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
    /// the linked pools per cmdlist type, managing handle association as well as additional
    /// bookkeeping data structures
    cmdlist_linked_pool_t mPoolDirect;
    cmdlist_linked_pool_t mPoolCompute;
    cmdlist_linked_pool_t mPoolCopy;

    /// parallel arrays to the pools, identically indexed
    /// the cmdlists must stay alive even while "unallocated"
    cc::array<ID3D12GraphicsCommandList5*> mRawListsDirect;
    cc::array<ID3D12GraphicsCommandList5*> mRawListsCompute;
    cc::array<ID3D12GraphicsCommandList5*> mRawListsCopy;
};
}
