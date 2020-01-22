#include "event_pool.hh"

#include <phantasm-hardware-interface/vulkan/common/log.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

pr::backend::handle::event pr::backend::vk::EventPool::createEvent()
{
    unsigned pool_index;
    {
        auto lg = std::lock_guard(mMutex);
        pool_index = mPool.acquire();
    }

    VkEventCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;

    VkEvent& new_event = mPool.get(pool_index);
    PR_VK_VERIFY_SUCCESS(vkCreateEvent(mDevice, &info, nullptr, &new_event));

    return {static_cast<handle::index_t>(pool_index)};
}

void pr::backend::vk::EventPool::free(pr::backend::handle::event event)
{
    if (!event.is_valid())
        return;

    VkEvent freed_event = mPool.get(static_cast<unsigned>(event.index));
    vkDestroyEvent(mDevice, freed_event, nullptr);

    {
        auto lg = std::lock_guard(mMutex);
        mPool.release(static_cast<unsigned>(event.index));
    }
}

void pr::backend::vk::EventPool::free(cc::span<const pr::backend::handle::event> event_span)
{
    auto lg = std::lock_guard(mMutex);

    for (auto as : event_span)
    {
        if (as.is_valid())
        {
            VkEvent freed_event = mPool.get(static_cast<unsigned>(as.index));
            vkDestroyEvent(mDevice, freed_event, nullptr);
            mPool.release(static_cast<unsigned>(as.index));
        }
    }
}

void pr::backend::vk::EventPool::initialize(VkDevice device, unsigned max_num_events)
{
    CC_ASSERT(mDevice == nullptr && "double init");
    mDevice = device;
    mPool.initialize(max_num_events);
}

void pr::backend::vk::EventPool::destroy()
{
    if (mDevice != nullptr)
    {
        auto num_leaks = 0;
        mPool.iterate_allocated_nodes([&](VkEvent leaked_event, unsigned) {
            ++num_leaks;
            vkDestroyEvent(mDevice, leaked_event, nullptr);
        });

        if (num_leaks > 0)
        {
            log::info()("warning: leaked {} handle::event object{}", num_leaks, num_leaks == 1 ? "" : "s");
        }
    }
}
