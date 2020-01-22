#include "event_pool.hh"

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/d3d12/common/log.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

pr::backend::handle::event pr::backend::d3d12::EventPool::createEvent()
{
    unsigned pool_index;
    {
        auto lg = std::lock_guard(mMutex);
        pool_index = mPool.acquire();
    }

    ID3D12Fence*& new_fence = mPool.get(pool_index);
    mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&new_fence));

    return {static_cast<handle::index_t>(pool_index)};
}

void pr::backend::d3d12::EventPool::free(pr::backend::handle::event event)
{
    if (!event.is_valid())
        return;

    mPool.get(static_cast<unsigned>(event.index))->Release();

    {
        auto lg = std::lock_guard(mMutex);
        mPool.release(static_cast<unsigned>(event.index));
    }
}

void pr::backend::d3d12::EventPool::free(cc::span<const pr::backend::handle::event> event_span)
{
    auto lg = std::lock_guard(mMutex);

    for (auto as : event_span)
    {
        if (as.is_valid())
        {
            mPool.get(static_cast<unsigned>(as.index))->Release();
            mPool.release(static_cast<unsigned>(as.index));
        }
    }
}

void pr::backend::d3d12::EventPool::initialize(ID3D12Device* device, unsigned max_num_events)
{
    CC_ASSERT(mDevice == nullptr && "double init");
    mDevice = device;
    mPool.initialize(max_num_events);
}

void pr::backend::d3d12::EventPool::destroy()
{
    if (mDevice != nullptr)
    {
        auto num_leaks = 0;
        mPool.iterate_allocated_nodes([&](ID3D12Fence* leaked_event, unsigned) {
            ++num_leaks;
            leaked_event->Release();
        });

        if (num_leaks > 0)
        {
            log::info()("warning: leaked {} handle::event object{}", num_leaks, num_leaks == 1 ? "" : "s");
        }
    }
}
