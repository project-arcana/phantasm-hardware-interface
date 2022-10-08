#pragma once

#include <cstdint>

#include "common/d3d12_fwd.hh"

namespace phi::d3d12
{
class SimpleFence
{
public:
    void initialize(ID3D12Device& device);
    void destroy();

    void signalCPU(uint64_t new_val);
    void signalGPU(uint64_t new_val, ID3D12CommandQueue& queue);

    // returns true if a wait occured
    bool waitCPU(uint64_t val);

    void waitGPU(uint64_t val, ID3D12CommandQueue& queue);

    uint64_t getCurrentValue() const;

    ID3D12Fence* fence = nullptr;
    ::HANDLE event;
};

class Fence
{
public:
    void initialize(ID3D12Device& device) { mFence.initialize(device); }
    void destroy() { mFence.destroy(); }

    void issueFence(ID3D12CommandQueue& queue)
    {
        ++mCounter;
        mFence.signalGPU(mCounter, queue);
    }

    void waitOnCPU(uint64_t old_fence)
    {
        if (mCounter > old_fence)
        {
            mFence.waitCPU(mCounter - old_fence);
        }
    }

    void waitOnGPU(ID3D12CommandQueue& queue) { mFence.waitGPU(mCounter, queue); }

    [[nodiscard]] ID3D12Fence* getRawFence() const { return mFence.fence; }

private:
    SimpleFence mFence;
    uint64_t mCounter = 0;
};

}
