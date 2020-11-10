#pragma once

#include <phantasm-hardware-interface/common/container/linked_pool.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/vulkan_fwd.hh>

namespace phi::vk
{
class FencePool
{
public:
    [[nodiscard]] handle::fence createFence();

    void free(handle::fence fence);
    void free(cc::span<handle::fence const> fence_span);

public:
    void initialize(VkDevice device, unsigned max_num_fences, cc::allocator* static_alloc);
    void destroy();

    VkSemaphore get(handle::fence fence) const
    {
        CC_ASSERT(fence.is_valid() && "invalid handle::fence");
        return mPool.get(fence._value);
    }

    void signalCPU(handle::fence fence, uint64_t val) const;
    void waitCPU(handle::fence fence, uint64_t val) const;

    [[nodiscard]] uint64_t getValue(handle::fence fence) const;

private:
    VkDevice mDevice = nullptr;

    phi::linked_pool<VkSemaphore> mPool;
};

}
