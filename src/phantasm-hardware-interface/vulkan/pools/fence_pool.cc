#include "fence_pool.hh"

#include <clean-core/capped_array.hh>

#include <phantasm-hardware-interface/common/log.hh>

#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

phi::handle::fence phi::vk::FencePool::createFence()
{
    unsigned pool_index = mPool.acquire();


    VkSemaphoreTypeCreateInfo sem_type_info = {};
    sem_type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    sem_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    sem_type_info.initialValue = 0;

    VkSemaphoreCreateInfo sem_info = {};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sem_info.pNext = &sem_type_info;

    VkSemaphore& new_sem = mPool.get(pool_index);
    PHI_VK_VERIFY_SUCCESS(vkCreateSemaphore(mDevice, &sem_info, nullptr, &new_sem));

    return {static_cast<handle::handle_t>(pool_index)};
}

void phi::vk::FencePool::free(phi::handle::fence fence)
{
    if (!fence.is_valid())
        return;

    VkSemaphore freed_fence = mPool.get(fence._value);
    vkDestroySemaphore(mDevice, freed_fence, nullptr);

    mPool.release(fence._value);
}

void phi::vk::FencePool::free(cc::span<const phi::handle::fence> fence_span)
{
    for (auto as : fence_span)
    {
        if (as.is_valid())
        {
            VkSemaphore freed_fence = mPool.get(as._value);
            vkDestroySemaphore(mDevice, freed_fence, nullptr);
            mPool.release(as._value);
        }
    }
}

void phi::vk::FencePool::initialize(VkDevice device, unsigned max_num_fences, cc::allocator* static_alloc)
{
    CC_ASSERT(mDevice == nullptr && "double init");
    mDevice = device;
    mPool.initialize(max_num_fences, static_alloc);
}

void phi::vk::FencePool::destroy()
{
    if (mDevice != nullptr)
    {
        auto num_leaks = 0;
        mPool.iterate_allocated_nodes([&](VkSemaphore leaked_fence) {
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

    PHI_VK_VERIFY_SUCCESS(vkSignalSemaphore(mDevice, &info));
}

void phi::vk::FencePool::waitCPU(phi::handle::fence fence, uint64_t val) const
{
    VkSemaphore sem = get(fence);

    VkSemaphoreWaitInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.semaphoreCount = 1;
    info.pSemaphores = &sem;
    info.pValues = &val;

    PHI_VK_VERIFY_SUCCESS(vkWaitSemaphores(mDevice, &info, UINT64_MAX));
}

uint64_t phi::vk::FencePool::getValue(phi::handle::fence fence) const
{
    uint64_t res;
    PHI_VK_VERIFY_SUCCESS(vkGetSemaphoreCounterValue(mDevice, get(fence), &res));
    return res;
}
