#pragma once

#include <cstdint>

#include "common/d3d12_sanitized.hh"
#include "common/shared_com_ptr.hh"
#include "common/verify.hh"

namespace phi::d3d12
{
class SimpleFence
{
public:
    void initialize(ID3D12Device& device);
    ~SimpleFence();

    SimpleFence() = default;
    SimpleFence(SimpleFence const&) = delete;
    SimpleFence& operator=(SimpleFence const&) = delete;
    SimpleFence(SimpleFence&&) noexcept = delete;
    SimpleFence& operator=(SimpleFence&&) noexcept = delete;

    void signalCPU(uint64_t new_val) { mFence->Signal(new_val); }
    void signalGPU(uint64_t new_val, ID3D12CommandQueue& queue) { queue.Signal(mFence, new_val); }

    void waitCPU(uint64_t val);
    void waitGPU(uint64_t val, ID3D12CommandQueue& queue);

    [[nodiscard]] uint64_t getCurrentValue() const
    {
        auto const res = mFence->GetCompletedValue();
#ifdef CC_ENABLE_ASSERTIONS
        PR_D3D12_DRED_ASSERT(res != UINT64_MAX, mFence);
#endif
        return res;
    }

    [[nodiscard]] ID3D12Fence* getRawFence() const { return mFence.get(); }
    [[nodiscard]] HANDLE getRawEvent() const { return mEvent; }

private:
    shared_com_ptr<ID3D12Fence> mFence;
    HANDLE mEvent;
};

class Fence
{
public:
    void initialize(ID3D12Device& device) { mFence.initialize(device); }

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

    [[nodiscard]] ID3D12Fence* getRawFence() const { return mFence.getRawFence(); }

private:
    SimpleFence mFence;
    uint64_t mCounter = 0;
};

}
