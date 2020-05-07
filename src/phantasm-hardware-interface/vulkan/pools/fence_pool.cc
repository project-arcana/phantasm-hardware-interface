#include "fence_pool.hh"

#include <clean-core/capped_array.hh>

#include <phantasm-hardware-interface/detail/log.hh>

#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace
{
constexpr VkPipelineStageFlags const gc_wait_dst_masks[8]
    = {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
}

phi::handle::fence phi::vk::FencePool::createFence()
{
    unsigned pool_index;
    {
        auto lg = std::lock_guard(mMutex);
        pool_index = mPool.acquire();
    }

    VkSemaphoreTypeCreateInfo sem_type_info = {};
    sem_type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    sem_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    sem_type_info.initialValue = 0;

    VkSemaphoreCreateInfo sem_info = {};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sem_info.pNext = &sem_type_info;

    VkSemaphore& new_sem = mPool.get(pool_index);
    PHI_VK_VERIFY_SUCCESS(vkCreateSemaphore(mDevice, &sem_info, nullptr, &new_sem));

    return {static_cast<handle::index_t>(pool_index)};
}

void phi::vk::FencePool::free(phi::handle::fence fence)
{
    if (!fence.is_valid())
        return;

    VkSemaphore freed_fence = mPool.get(static_cast<unsigned>(fence.index));
    vkDestroySemaphore(mDevice, freed_fence, nullptr);

    {
        auto lg = std::lock_guard(mMutex);
        mPool.release(static_cast<unsigned>(fence.index));
    }
}

void phi::vk::FencePool::free(cc::span<const phi::handle::fence> fence_span)
{
    auto lg = std::lock_guard(mMutex);

    for (auto as : fence_span)
    {
        if (as.is_valid())
        {
            VkSemaphore freed_fence = mPool.get(static_cast<unsigned>(as.index));
            vkDestroySemaphore(mDevice, freed_fence, nullptr);
            mPool.release(static_cast<unsigned>(as.index));
        }
    }
}

void phi::vk::FencePool::initialize(VkDevice device, unsigned max_num_fences)
{
    CC_ASSERT(mDevice == nullptr && "double init");
    mDevice = device;
    mPool.initialize(max_num_fences);
}

void phi::vk::FencePool::destroy()
{
    if (mDevice != nullptr)
    {
        auto num_leaks = 0;
        mPool.iterate_allocated_nodes([&](VkSemaphore leaked_fence, unsigned) {
            ++num_leaks;
            vkDestroySemaphore(mDevice, leaked_fence, nullptr);
        });

        if (num_leaks > 0)
        {
            PHI_LOG("leaked {} handle::fence object{}", num_leaks, num_leaks == 1 ? "" : "s");
        }
    }
}

void phi::vk::FencePool::signalCPU(phi::handle::fence fence, uint64_t val) const
{
    VkSemaphoreSignalInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    info.semaphore = get(fence);
    info.value = val;

    PHI_VK_VERIFY_SUCCESS(vkSignalSemaphoreKHR(mDevice, &info));
}

void phi::vk::FencePool::signalGPU(phi::handle::fence fence, uint64_t val, VkQueue queue) const
{
    signalWaitGPU(cc::span{fence}, cc::span{val}, {}, {}, queue);
}

void phi::vk::FencePool::waitCPU(phi::handle::fence fence, uint64_t val) const
{
    VkSemaphore sem = get(fence);

    VkSemaphoreWaitInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.semaphoreCount = 1;
    info.pSemaphores = &sem;
    info.pValues = &val;

    // NOTE: KHR! We're not on Vulkan 1.2, these are the extension timeline semaphores and not the 1.2 core ones
    PHI_VK_VERIFY_SUCCESS(vkWaitSemaphoresKHR(mDevice, &info, UINT64_MAX));
}

void phi::vk::FencePool::waitGPU(phi::handle::fence fence, uint64_t val, VkQueue queue) const
{
    signalWaitGPU({}, {}, cc::span{fence}, cc::span{val}, queue);
}

void phi::vk::FencePool::signalWaitGPU(cc::span<const phi::handle::fence> signal_fences,
                                       cc::span<const uint64_t> signal_vals,
                                       cc::span<const phi::handle::fence> wait_fences,
                                       cc::span<const uint64_t> wait_vals,
                                       VkQueue queue) const
{
    cc::capped_array<VkSemaphore, 8> wait_sems;
    wait_sems.emplace(wait_fences.size());
    for (auto i = 0u; i < wait_sems.size(); ++i)
        wait_sems[i] = get(wait_fences[i]);

    cc::capped_array<VkSemaphore, 8> signal_sems;
    signal_sems.emplace(signal_fences.size());
    for (auto i = 0u; i < signal_sems.size(); ++i)
        signal_sems[i] = get(signal_fences[i]);

    VkTimelineSemaphoreSubmitInfoKHR timelineInfo = {};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
    timelineInfo.waitSemaphoreValueCount = uint32_t(wait_vals.size());
    timelineInfo.pWaitSemaphoreValues = wait_vals.data();
    timelineInfo.signalSemaphoreValueCount = uint32_t(signal_vals.size());
    timelineInfo.pSignalSemaphoreValues = signal_vals.data();

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.waitSemaphoreCount = uint32_t(wait_sems.size());
    submitInfo.pWaitSemaphores = wait_sems.empty() ? nullptr : wait_sems.data();
    submitInfo.signalSemaphoreCount = uint32_t(signal_sems.size());
    submitInfo.pSignalSemaphores = signal_sems.empty() ? nullptr : signal_sems.data();
    submitInfo.pWaitDstStageMask = gc_wait_dst_masks;

    PHI_VK_VERIFY_SUCCESS(vkQueueSubmit(queue, 1, &submitInfo, nullptr));
}

uint64_t phi::vk::FencePool::getValue(phi::handle::fence fence) const
{
    uint64_t res;
    // NOTE: KHR! We're not on Vulkan 1.2, these are the extension timeline semaphores and not the 1.2 core ones
    PHI_VK_VERIFY_SUCCESS(vkGetSemaphoreCounterValueKHR(mDevice, get(fence), &res));
    return res;
}
