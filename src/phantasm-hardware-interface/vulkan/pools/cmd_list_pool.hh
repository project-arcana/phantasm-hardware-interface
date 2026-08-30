#pragma once

#include <atomic>
#include <mutex>

#include <clean-core/alloc_array.hh>
#include <clean-core/alloc_vector.hh>
#include <clean-core/atomic_linked_pool.hh>

#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/common/vk_incomplete_state_cache.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk
{
class Device;

/// Ringbuffer for fences used for internal submit sync
/// Unsynchronized - 1 per CommandListPool
class FenceRingbuffer
{
public:
    void initialize(VkDevice device, unsigned num_fences, cc::allocator* static_alloc);
    void destroy(VkDevice device);

public:
    /// acquires a fence from the ringbuffer
    /// not thread safe, must not be called concurrently from multiple places
    /// the returned fence has an initial refcount of 1
    [[nodiscard]] unsigned acquireFence(VkDevice device, VkFence& out_fence);

    /// block until the fence at the given index is signalled
    /// thread safe
    void waitForFence(VkDevice device, unsigned index) const;

    /// returns true if the fence at the given index is signalled
    /// thread safe
    [[nodiscard]] bool isFenceSignalled(VkDevice device, unsigned index) const
    {
        CC_ASSERT(mFences[index].ref_count.load() > 0);
        return vkGetFenceStatus(device, mFences[index].raw_fence) == VK_SUCCESS;
    }

    /// increments the refcount of the fence at the given index
    /// thread safe
    void incrementRefcount(unsigned index, int amount = 1)
    {
        auto const pre_increment = mFences[index].ref_count.fetch_add(amount);
        CC_ASSERT(pre_increment >= 0);
    }

    /// decrements the refcount of the fence at the given index
    /// thread safe
    void decrementRefcount(unsigned index)
    {
        auto const pre_decrement = mFences[index].ref_count.fetch_sub(1);
        CC_ASSERT(pre_decrement >= 1);
    }

private:
    struct fence_node
    {
        VkFence raw_fence;
        /// the amount of allocators depending on this fence
        std::atomic_int ref_count;
    };

    cc::alloc_array<fence_node> mFences;
    unsigned mNextFence = 0;
};

/// A single command allocator that keeps track of its lists
/// Unsynchronized - N per CommandAllocatorBundle
struct CommandAllocator
{
public:
    void initialize(VkDevice device, unsigned num_cmd_lists, unsigned queue_family_index, FenceRingbuffer* fence_ring, cc::allocator* static_alloc, cc::allocator* dynamic_alloc);
    void destroy(VkDevice device);

public:
    /// returns true if this node is full
    [[nodiscard]] bool is_full() const { return _num_in_flight == _cmd_buffers.size(); }

    /// returns true if this node is full and capable of resetting
    [[nodiscard]] bool can_reset() const { return is_full() && is_submit_counter_up_to_date(); }

    /// acquire a command buffer from this allocator
    /// do not call if full (best case: blocking, worst case: crash)
    [[nodiscard]] VkCommandBuffer acquire(VkDevice device);

    /// to be called when a command buffer backed by this allocator
    /// is being dicarded (will never result in a submit)
    /// free-threaded
    void on_discard(unsigned num = 1) { _num_discarded.fetch_add(num); }

    /// to be called when a command buffer backed by this allocator
    /// is being submitted, along with the (pre-refcount-incremented) fence index that was
    /// used during the submission
    /// free-threaded
    void on_submit(unsigned num, unsigned fence_index);

    /// non-blocking reset attempt
    /// returns true if the allocator is usable afterwards
    [[nodiscard]] bool try_reset(VkDevice device);

    /// blocking reset attempt
    /// returns true if the allocator is usable afterwards
    [[nodiscard]] bool try_reset_blocking(VkDevice device);

    /// add an associated framebuffer which will be destroyed on the next reset
    void add_associated_framebuffer(VkFramebuffer fb, cc::span<VkImageView const> image_views)
    {
        _associated_framebuffers.push_back(fb);
        for (auto iv : image_views)
            _associated_framebuffer_image_views.push_back(iv);
    }

private:
    bool is_submit_counter_up_to_date() const
    {
        // _num_in_flight is synchronized as this method is only called from the owning thread
        // the load order on the other two atomics does not matter since they monotonously increase
        // and never surpass _num_in_flight
        return _num_in_flight == _num_discarded + _num_pending_execution;
    }

    void do_reset(VkDevice device);

private:
    // non-owning
    FenceRingbuffer* _fence_ring;

    VkCommandPool _cmd_pool;
    cc::alloc_array<VkCommandBuffer> _cmd_buffers;

    /// amount of cmdbufs given out
    unsigned _num_in_flight = 0;
    /// amount of cmdbufs discarded, always less or equal to num_in_flight
    /// discarded command buffers cannot be reused, we always have to reset the entire pool
    std::atomic_uint _num_discarded = 0;
    /// amount of cmdbufs submitted, always less or equal to num_in_flight
    /// if #discard + #pending_exec == #in_flight, we can start making decisions about resetting
    std::atomic_uint _num_pending_execution = 0;

    /// the most recent fence index, -1u if none
    std::atomic_uint _latest_fence = unsigned(-1);

    /// a storage for VkFramebuffers which have been created during recording of the command buffers
    /// created by this allocator. Recording threads add their created framebuffers, and the list gets
    /// destroyed on reset, guaranteeing that all of them are no longer in flight
    cc::alloc_vector<VkFramebuffer> _associated_framebuffers;

    /// Framebuffers require their image views to stay alive as well
    cc::alloc_vector<VkImageView> _associated_framebuffer_image_views;
};

/// A bundle of single command allocators which automatically
/// circles through them and soft-resets when possible
/// Unsynchronized - 1 per thread, per queue type
class CommandAllocatorBundle
{
public:
    void initialize(VkDevice device,
                    unsigned num_allocators,
                    unsigned num_cmdlists_per_allocator,
                    unsigned queue_family_index,
                    FenceRingbuffer* fence_ring,
                    cc::allocator* static_alloc,
                    cc::allocator* dynamic_alloc);
    void destroy(VkDevice device);

    /// Resets the given command list to use memory by an appropriate allocator
    /// Returns a pointer to the backing allocator node
    CommandAllocator* acquireMemory(VkDevice device, VkCommandBuffer& out_buffer);

private:
    void updateActiveIndex(VkDevice device);

private:
    cc::alloc_array<CommandAllocator> mAllocators;
    size_t mActiveAllocator = 0u;
};

struct CommandAllocatorsPerThread
{
    CommandAllocatorBundle bundle_direct;
    CommandAllocatorBundle bundle_compute;
    CommandAllocatorBundle bundle_copy;

    void destroy(VkDevice device)
    {
        bundle_direct.destroy(device);
        bundle_compute.destroy(device);
        bundle_copy.destroy(device);
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

class BackendVulkan;

/// The high-level allocator for Command Lists
/// Synchronized - 1 per application
class CommandListPool
{
public:
    // frontend-facing API (not quite, command_list can only be compiled immediately)

    [[nodiscard]] handle::command_list create(VkCommandBuffer& out_cmdlist, CommandAllocatorsPerThread& thread_allocator, queue_type type);

    /// acquire a fence to be used for command buffer submission, returns the index
    /// ONLY use the resulting index ONCE in either of the two freeOnSubmit overloads
    [[nodiscard]] unsigned acquireFence(VkFence& out_fence) { return mFenceRing.acquireFence(mDevice, out_fence); }

    /// to be called when the given command lists have been submitted, alongside the fence index that was used
    /// the cmdlists and the fence index are now consumed and must not be reused
    void freeOnSubmit(handle::command_list cl, unsigned fence_index);
    void freeOnSubmit(cc::span<handle::command_list const> cls, unsigned fence_index);
    void freeOnSubmit(cc::span<cc::span<handle::command_list const> const> cls_nested, unsigned fence_index);

    /// to be called when the given command lists will not be submitted down the line
    /// the cmdlists are now consumed and must not be reused
    void freeAndDiscard(cc::span<handle::command_list const> cls);

    /// discards all command lists that are currently alive
    /// all cmdlists acquired before this call are now consumed and must not be reused
    /// returns the amount of cmdlists that were freed
    unsigned discardAndFreeAll();


public:
    struct cmd_list_node
    {
        // an allocated node is always in the following state:
        // - the command list is freshly reset using an appropriate allocator
        // - the pResponsibleAllocator must be informed on submit or discard
        CommandAllocator* pResponsibleAllocator;
        vk_incomplete_state_cache state_cache;
        VkCommandBuffer raw_buffer;
    };

    using cmdlist_linked_pool_t = cc::atomic_linked_pool<cmd_list_node>;

public:
    // internal API

    [[nodiscard]] cmd_list_node& getCommandListNode(handle::command_list cl) { return mPool.get(cl._value); }
    [[nodiscard]] cmd_list_node const& getCommandListNode(handle::command_list cl) const { return mPool.get(cl._value); }

    [[nodiscard]] VkCommandBuffer getRawBuffer(handle::command_list cl) const { return getCommandListNode(cl).raw_buffer; }

    [[nodiscard]] vk_incomplete_state_cache* getStateCache(handle::command_list cl) { return &getCommandListNode(cl).state_cache; }

    void addAssociatedFramebuffer(handle::command_list cl, VkFramebuffer fb, cc::span<VkImageView const> imgviews)
    {
        getCommandListNode(cl).pResponsibleAllocator->add_associated_framebuffer(fb, imgviews);
    }

public:
    void initialize(Device& device,
                    int num_direct_allocs,
                    int num_direct_lists_per_alloc,
                    int num_compute_allocs,
                    int num_compute_lists_per_alloc,
                    int num_copy_allocs,
                    int num_copy_lists_per_alloc,
                    int max_num_unique_transitions_per_cmdlist,
                    cc::span<CommandAllocatorsPerThread*> thread_allocators,
                    cc::allocator* static_alloc,
                    cc::allocator* dynamic_alloc);
    void destroy();

private:
    // non-owning
    VkDevice mDevice;

    // the fence ringbuffer
    FenceRingbuffer mFenceRing;

    // the linked pool
    cmdlist_linked_pool_t mPool;

    // flat memory for the state caches
    int mNumStateCacheEntriesPerCmdlist;
    cc::alloc_array<vk_incomplete_state_cache::cache_entry> mFlatStateCacheEntries;

    std::mutex mMutex;
};

}
