#include "Fence.hh"

#include <clean-core/assert.hh>

#include "common/d3d12_sanitized.hh"
#include "common/verify.hh"

void phi::d3d12::SimpleFence::initialize(ID3D12Device& device)
{
    CC_ASSERT(fence == nullptr && "double init");
    event = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
    CC_ASSERT(event != INVALID_HANDLE_VALUE && "failed to create win32 event");
    PHI_D3D12_VERIFY(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
}

void phi::d3d12::SimpleFence::destroy()
{
    if (fence != nullptr)
    {
        fence->Release();
        fence = nullptr;
        ::CloseHandle(event);
    }
}

void phi::d3d12::SimpleFence::signalCPU(uint64_t new_val) { fence->Signal(new_val); }

void phi::d3d12::SimpleFence::signalGPU(uint64_t new_val, ID3D12CommandQueue& queue) { queue.Signal(fence, new_val); }

bool phi::d3d12::SimpleFence::waitCPU(uint64_t val)
{
    if (fence->GetCompletedValue() <= val)
    {
        PHI_D3D12_VERIFY(fence->SetEventOnCompletion(val, event));
        ::WaitForSingleObject(event, INFINITE);
        return true;
    }

    return false;
}

void phi::d3d12::SimpleFence::waitGPU(uint64_t val, ID3D12CommandQueue& queue)
{
    //
    PHI_D3D12_VERIFY(queue.Wait(fence, val));
}

uint64_t phi::d3d12::SimpleFence::getCurrentValue() const
{
    auto const res = fence->GetCompletedValue();
#ifdef CC_ENABLE_ASSERTIONS
    PHI_D3D12_DRED_ASSERT(res != UINT64_MAX, fence);
#endif
    return res;
}
