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
    [[nodiscard]] handle::fence createEvent();

    void free(handle::fence event);
    void free(cc::span<handle::fence const> event_span);

public:
    void initialize(VkDevice device, unsigned max_num_events);
    void destroy();

    VkSemaphore get(handle::fence event) const
    {
        CC_ASSERT(event.is_valid() && "invalid handle::event");
        return mPool.get(static_cast<unsigned>(event.index));
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
