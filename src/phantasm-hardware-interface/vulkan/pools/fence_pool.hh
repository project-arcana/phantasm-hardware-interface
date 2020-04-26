#pragma once

#include <mutex>

#include <phantasm-hardware-interface/detail/linked_pool.hh>
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
    void initialize(VkDevice device, unsigned max_num_fences);
    void destroy();

    VkSemaphore get(handle::fence fence) const
    {
        CC_ASSERT(fence.is_valid() && "invalid handle::fence");
        return mPool.get(static_cast<unsigned>(fence.index));
    }

    void signalCPU(handle::fence fence, uint64_t val) const;
    void signalGPU(handle::fence fence, uint64_t val, VkQueue queue) const;

    void waitCPU(handle::fence fence, uint64_t val) const;
    void waitGPU(handle::fence fence, uint64_t val, VkQueue queue) const;

    [[nodiscard]] uint64_t getValue(handle::fence fence) const;

    void signalWaitGPU(cc::span<handle::fence const> signal_fences,
                       cc::span<uint64_t const> signal_vals,
                       cc::span<handle::fence const> wait_fences,
                       cc::span<uint64_t const> wait_vals,
                       VkQueue queue) const;

private:
    VkDevice mDevice = nullptr;

    phi::detail::linked_pool<VkSemaphore, unsigned> mPool;

    std::mutex mMutex;
};

}
