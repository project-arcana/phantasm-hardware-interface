#pragma once

#include <phantasm-hardware-interface/types.hh>

#include "common/d3d12_fwd.hh"
#include "common/shared_com_ptr.hh"

namespace pr::backend::d3d12
{

class Queue
{
    // reference type
public:
    Queue() = default;
    Queue(Queue const&) = delete;
    Queue(Queue&&) noexcept = delete;
    Queue& operator=(Queue const&) = delete;
    Queue& operator=(Queue&&) noexcept = delete;

    void initialize(ID3D12Device& device, queue_type type = queue_type::graphics);

    [[nodiscard]] ID3D12CommandQueue& getQueue() const { return *mQueue.get(); }
    [[nodiscard]] shared_com_ptr<ID3D12CommandQueue> const& getQueueShared() const { return mQueue; }

private:
    shared_com_ptr<ID3D12CommandQueue> mQueue;
};

}
