#pragma once

#include <mutex>

#include <phantasm-hardware-interface/detail/linked_pool.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>

namespace phi::d3d12
{
class EventPool
{
public:
    [[nodiscard]] handle::event createEvent();

    void free(handle::event event);
    void free(cc::span<handle::event const> event_span);

public:
    void initialize(ID3D12Device* device, unsigned max_num_events);
    void destroy();

    ID3D12Fence* get(handle::event event) const
    {
        CC_ASSERT(event.is_valid() && "invalid handle::event");
        return mPool.get(static_cast<unsigned>(event.index));
    }

private:
    ID3D12Device* mDevice = nullptr;

    phi::detail::linked_pool<ID3D12Fence*, unsigned> mPool;

    std::mutex mMutex;
};

}
