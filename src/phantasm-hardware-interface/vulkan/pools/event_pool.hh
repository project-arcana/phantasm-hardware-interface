#pragma once

#include <mutex>

#include <phantasm-hardware-interface/detail/linked_pool.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/vulkan_fwd.hh>

namespace phi::vk
{
class EventPool
{
public:
    [[nodiscard]] handle::event createEvent();

    void free(handle::event event);
    void free(cc::span<handle::event const> event_span);

public:
    void initialize(VkDevice device, unsigned max_num_events);
    void destroy();

    VkEvent get(handle::event event) const
    {
        CC_ASSERT(event.is_valid() && "invalid handle::event");
        return mPool.get(static_cast<unsigned>(event.index));
    }

private:
    VkDevice mDevice = nullptr;

    phi::detail::linked_pool<VkEvent, unsigned> mPool;

    std::mutex mMutex;
};

}
