#include "cmd_list_pool.hh"

#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/detail/flat_map.hh>
#include <phantasm-hardware-interface/vulkan/BackendVulkan.hh>
#include <phantasm-hardware-interface/vulkan/common/log.hh>
#include <phantasm-hardware-interface/vulkan/common/util.hh>

void phi::vk::cmd_allocator_node::initialize(VkDevice device, unsigned num_cmd_lists, unsigned queue_family_index, FenceRingbuffer* fence_ring)
{
    _fence_ring = fence_ring;

    // create pool
    {
        VkCommandPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.queueFamilyIndex = queue_family_index;
        info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        PHI_VK_VERIFY_SUCCESS(vkCreateCommandPool(device, &info, nullptr, &_cmd_pool));
    }
    // allocate buffers
    {
        _cmd_buffers = _cmd_buffers.uninitialized(num_cmd_lists);

        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = _cmd_pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = num_cmd_lists;

        PHI_VK_VERIFY_SUCCESS(vkAllocateCommandBuffers(device, &info, _cmd_buffers.data()));
    }

    _associated_framebuffers.reserve(num_cmd_lists * 3);                                                            // arbitrary
    _associated_framebuffer_image_views.resize(_associated_framebuffers.size() * (limits::max_render_targets + 1)); // num render targets + depthstencil

    _latest_fence.store(unsigned(-1));
}

void phi::vk::cmd_allocator_node::destroy(VkDevice device)
{
    do_reset(device);
    vkDestroyCommandPool(device, _cmd_pool, nullptr);
}

VkCommandBuffer phi::vk::cmd_allocator_node::acquire(VkDevice device)
{
    if (is_full())
    {
        // the allocator is full, we are almost dead but might be able to reset
        auto const reset_success = try_reset_blocking(device);
        CC_RUNTIME_ASSERT(reset_success && "cmdlist allocator node overcommitted and unable to recover");
        // we were able to recover, but should warn even now
    }

    auto const res = _cmd_buffers[_num_in_flight];
    ++_num_in_flight;

    VkCommandBufferBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    PHI_VK_VERIFY_SUCCESS(vkBeginCommandBuffer(res, &info));

    return res;
}

void phi::vk::cmd_allocator_node::on_submit(unsigned num, unsigned fence_index)
{
    // first, update the latest fence
    auto const previous_fence = _latest_fence.exchange(fence_index);
    if (previous_fence != unsigned(-1) && previous_fence != fence_index)
    {
        // release previous fence
        _fence_ring->decrementRefcount(previous_fence);
    }

    // second, increment the pending execution counter, as it guards access to _latest_fence
    // (an increment here might turn is_submit_counter_up_to_date true)
    _num_pending_execution.fetch_add(num);
}

bool phi::vk::cmd_allocator_node::try_reset(VkDevice device)
{
    if (can_reset())
    {
        // full, and all acquired cmdbufs have been either submitted or discarded, check the fences

        if (_num_pending_execution.load() > 0)
        {
            // there was at least a single real submission, load the latest fance
            auto const relevant_fence = _latest_fence.load();
            CC_ASSERT(relevant_fence != unsigned(-1));

            if (_fence_ring->isFenceSignalled(device, relevant_fence))
            {
                // the fence is signalled, we can reset

                // decrement the refcount on the fence and reset the latest fence index
                _fence_ring->decrementRefcount(relevant_fence);
                _latest_fence.store(unsigned(-1));

                // perform the reset
                do_reset(device);
                return true;
            }
            else
            {
                // some fences are pending
                return false;
            }
        }
        else
        {
            // all cmdbuffers were discarded, we can reset unconditionally
            do_reset(device);
            return true;
        }
    }
    else
    {
        // can't reset
        return !is_full();
    }
}

bool phi::vk::cmd_allocator_node::try_reset_blocking(VkDevice device)
{
    if (can_reset())
    {
        // full, and all acquired cmdbufs have been either submitted or discarded, check the fences

        if (_num_pending_execution.load() > 0)
        {
            // there was at least a single real submission, load the latest fance
            auto const relevant_fence = _latest_fence.load();
            CC_ASSERT(relevant_fence != unsigned(-1));

            // block
            _fence_ring->waitForFence(device, relevant_fence);

            // decrement the refcount on the fence and reset the latest fence index
            _fence_ring->decrementRefcount(relevant_fence);
            _latest_fence.store(unsigned(-1));
        }

        do_reset(device);
        return true;
    }
    else
    {
        // can't reset
        return !is_full();
    }
}

void phi::vk::cmd_allocator_node::do_reset(VkDevice device)
{
    PHI_VK_VERIFY_SUCCESS(vkResetCommandPool(device, _cmd_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT));

    for (auto fb : _associated_framebuffers)
    {
        vkDestroyFramebuffer(device, fb, nullptr);
    }
    _associated_framebuffers.clear();

    for (auto iv : _associated_framebuffer_image_views)
    {
        vkDestroyImageView(device, iv, nullptr);
    }
    _associated_framebuffer_image_views.clear();

    _num_in_flight = 0;
    _num_discarded = 0;
    _num_pending_execution = 0;
}

void phi::vk::FenceRingbuffer::initialize(VkDevice device, unsigned num_fences)
{
    mFences = mFences.defaulted(num_fences);

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    for (auto i = 0u; i < mFences.size(); ++i)
    {
        auto& fence = mFences[i];
        fence.ref_count.store(0);
        PHI_VK_VERIFY_SUCCESS(vkCreateFence(device, &fence_info, nullptr, &fence.raw_fence));
        util::set_object_name(device, fence.raw_fence, "ringbuffer fence %u of %u", i, num_fences);
    }
}

void phi::vk::FenceRingbuffer::destroy(VkDevice device)
{
    for (auto& fence : mFences)
        vkDestroyFence(device, fence.raw_fence, nullptr);
}

unsigned phi::vk::FenceRingbuffer::acquireFence(VkDevice device, VkFence& out_fence)
{
    for (auto i = 0u; i < mFences.size(); ++i)
    {
        auto const fence_i = mNextFence;
        mNextFence = cc::wrapped_increment(mNextFence, unsigned(mFences.size()));

        auto& node = mFences[fence_i];

        if (node.ref_count.load() == 0)
        {
            vkResetFences(device, 1, &node.raw_fence);
            node.ref_count.store(1); // set the refcount to one
            out_fence = node.raw_fence;
            return fence_i;
        }
    }

    // none of the fences are resettable
    CC_RUNTIME_ASSERT(false && "Fence ringbuffer is full");
    return 0;
}

void phi::vk::FenceRingbuffer::waitForFence(VkDevice device, unsigned index) const
{
    auto& node = mFences[index];
    CC_ASSERT(node.ref_count.load() > 0);
    auto const vkres = vkWaitForFences(device, 1, &node.raw_fence, VK_TRUE, UINT64_MAX);
    CC_ASSERT(vkres == VK_SUCCESS); // other cases are TIMEOUT (2^64 ns > 584 years) or DEVICE_LOST (dead anyway)
}

void phi::vk::CommandAllocatorBundle::initialize(
    VkDevice device, unsigned num_allocators, unsigned num_cmdlists_per_allocator, unsigned queue_family_index, phi::vk::FenceRingbuffer* fence_ring)
{
    mAllocators = mAllocators.defaulted(num_allocators);
    mActiveAllocator = 0u;

    for (cmd_allocator_node& alloc_node : mAllocators)
    {
        alloc_node.initialize(device, num_cmdlists_per_allocator, queue_family_index, fence_ring);
    }
}

void phi::vk::CommandAllocatorBundle::destroy(VkDevice device)
{
    for (auto& alloc_node : mAllocators)
        alloc_node.destroy(device);
}

phi::vk::cmd_allocator_node* phi::vk::CommandAllocatorBundle::acquireMemory(VkDevice device, VkCommandBuffer& out_buffer)
{
    updateActiveIndex(device);
    auto& active_alloc = mAllocators[mActiveAllocator];
    out_buffer = active_alloc.acquire(device);
    return &active_alloc;
}

void phi::vk::CommandAllocatorBundle::updateActiveIndex(VkDevice device)
{
    for (auto it = 0u; it < mAllocators.size(); ++it)
    {
        if (!mAllocators[mActiveAllocator].is_full() || mAllocators[mActiveAllocator].try_reset(device))
            // not full, or nonblocking reset successful
            return;
        else
        {
            mActiveAllocator = cc::wrapped_increment(mActiveAllocator, mAllocators.size());
        }
    }

    // all non-blocking resets failed, try blocking now
    for (auto it = 0u; it < mAllocators.size(); ++it)
    {
        if (mAllocators[mActiveAllocator].try_reset_blocking(device))
            // blocking reset successful
            return;
        else
        {
            mActiveAllocator = cc::wrapped_increment(mActiveAllocator, mAllocators.size());
        }
    }

    // all allocators have at least 1 dangling cmdlist, we cannot recover
    CC_RUNTIME_ASSERT(false && "all allocators overcommitted and unresettable");
}

phi::handle::command_list phi::vk::CommandListPool::create(VkCommandBuffer& out_cmdlist, phi::vk::CommandAllocatorBundle& thread_allocator)
{
    unsigned res_handle;
    {
        auto lg = std::lock_guard(mMutex);
        res_handle = mPool.acquire();
    }

    cmd_list_node& new_node = mPool.get(res_handle);
    new_node.responsible_allocator = thread_allocator.acquireMemory(mDevice, new_node.raw_buffer);

    out_cmdlist = new_node.raw_buffer;
    return {static_cast<handle::index_t>(res_handle)};
}

void phi::vk::CommandListPool::freeOnSubmit(phi::handle::command_list cl, unsigned fence_index)
{
    cmd_list_node& freed_node = mPool.get(static_cast<unsigned>(cl.index));
    {
        auto lg = std::lock_guard(mMutex);
        freed_node.responsible_allocator->on_submit(1, fence_index);
        mPool.release(static_cast<unsigned>(cl.index));
    }
}

void phi::vk::CommandListPool::freeOnSubmit(cc::span<const phi::handle::command_list> cls, unsigned fence_index)
{
    phi::detail::capped_flat_map<cmd_allocator_node*, unsigned, 24> unique_allocators;

    // free the cls in the pool and gather the unique allocators
    {
        auto lg = std::lock_guard(mMutex);
        for (auto const& cl : cls)
        {
            if (cl == handle::null_command_list)
                continue;

            cmd_list_node& freed_node = mPool.get(static_cast<unsigned>(cl.index));
            unique_allocators.get_value(freed_node.responsible_allocator, 0u) += 1;
            mPool.release(static_cast<unsigned>(cl.index));
        }
    }

    if (!unique_allocators._nodes.empty())
    {
        // the given fence_index has a reference count of 1, increment it to the amount of unique allocators responsible
        if (unique_allocators._nodes.size() > 1)
            mFenceRing.incrementRefcount(fence_index, int(unique_allocators._nodes.size()) - 1);

        // notify all unique allocators
        for (auto const& unique_alloc : unique_allocators._nodes)
        {
            unique_alloc.key->on_submit(unique_alloc.val, fence_index);
        }
    }
}

void phi::vk::CommandListPool::freeOnSubmit(cc::span<const cc::span<const phi::handle::command_list>> cls_nested, unsigned fence_index)
{
    phi::detail::capped_flat_map<cmd_allocator_node*, unsigned, 24> unique_allocators;

    // free the cls in the pool and gather the unique allocators
    {
        auto lg = std::lock_guard(mMutex);
        for (auto const& cls : cls_nested)
            for (auto const& cl : cls)
            {
                if (cl == handle::null_command_list)
                    continue;

                cmd_list_node& freed_node = mPool.get(static_cast<unsigned>(cl.index));
                unique_allocators.get_value(freed_node.responsible_allocator, 0u) += 1;
                mPool.release(static_cast<unsigned>(cl.index));
            }
    }

    if (!unique_allocators._nodes.empty())
    {
        // the given fence_index has a reference count of 1, increment it to the amount of unique allocators responsible
        if (unique_allocators._nodes.size() > 1)
            mFenceRing.incrementRefcount(fence_index, int(unique_allocators._nodes.size()) - 1);

        // notify all unique allocators
        for (auto const& unique_alloc : unique_allocators._nodes)
        {
            unique_alloc.key->on_submit(unique_alloc.val, fence_index);
        }
    }
}

void phi::vk::CommandListPool::freeAndDiscard(cc::span<const handle::command_list> cls)
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

unsigned phi::vk::CommandListPool::discardAndFreeAll()
{
    auto lg = std::lock_guard(mMutex);

    auto num_freed = 0u;
    mPool.iterate_allocated_nodes([&](cmd_list_node& leaked_node, unsigned index) {
        ++num_freed;
        leaked_node.responsible_allocator->on_discard();
        mPool.release(index);
    });

    return num_freed;
}

void phi::vk::CommandListPool::initialize(phi::vk::BackendVulkan& backend,
                                          unsigned num_allocators_per_thread,
                                          unsigned num_cmdlists_per_allocator,
                                          cc::span<phi::vk::CommandAllocatorBundle*> thread_allocators)
{
    mDevice = backend.mDevice.getDevice();

    auto const num_cmdlists_per_thread = num_allocators_per_thread * num_cmdlists_per_allocator;
    auto const num_cmdlists_total = num_cmdlists_per_thread * thread_allocators.size();
    auto const num_allocators_total = num_allocators_per_thread * static_cast<unsigned>(thread_allocators.size());

    mPool.initialize(num_cmdlists_total);
    mFenceRing.initialize(mDevice, num_allocators_total + 5); // arbitrary safety buffer, should never be required


    auto const direct_queue_family = static_cast<unsigned>(backend.mDevice.getQueueFamilyDirect());
    for (auto i = 0u; i < thread_allocators.size(); ++i)
    {
        thread_allocators[i]->initialize(mDevice, num_allocators_per_thread, num_cmdlists_per_allocator, direct_queue_family, &mFenceRing);
    }

    // Flush the backend
    backend.flushGPU();
}

void phi::vk::CommandListPool::destroy()
{
    auto const num_leaks = discardAndFreeAll();
    if (num_leaks > 0)
    {
        log::info()("warning: leaked {} handle::command_list object{}", num_leaks, (num_leaks == 1 ? "" : "s"));
    }

    mFenceRing.destroy(mDevice);
}
