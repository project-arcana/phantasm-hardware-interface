#include "Fence.hh"

#include <clean-core/assert.hh>

#include "common/d3d12_sanitized.hh"
#include "common/verify.hh"

void phi::d3d12::SimpleFence::initialize(ID3D12Device& device)
{
    CC_ASSERT(!mFence.is_valid());
    mEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
    CC_ASSERT(mEvent != INVALID_HANDLE_VALUE && "failed to create win32 event");
    PHI_D3D12_VERIFY(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, PHI_COM_WRITE(mFence)));
}

void phi::d3d12::SimpleFence::destroy()
{
    if (mFence.is_valid())
    {
        mFence = nullptr;
        ::CloseHandle(mEvent);
    }
}


void phi::d3d12::SimpleFence::waitCPU(uint64_t val)
{
    if (mFence->GetCompletedValue() <= val)
    {
        PHI_D3D12_VERIFY(mFence->SetEventOnCompletion(val, mEvent));
        ::WaitForSingleObject(mEvent, INFINITE);
    }
}

void phi::d3d12::SimpleFence::waitGPU(uint64_t val, ID3D12CommandQueue& queue)
{
    //
    PHI_D3D12_VERIFY(queue.Wait(mFence, val));
}
