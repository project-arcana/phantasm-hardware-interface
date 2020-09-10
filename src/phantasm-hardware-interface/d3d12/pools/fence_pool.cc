#include "fence_pool.hh"

#include <phantasm-hardware-interface/detail/log.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

phi::handle::fence phi::d3d12::FencePool::createFence()
{
    unsigned pool_index;
    {
        auto lg = std::lock_guard(mMutex);
        pool_index = mPool.acquire();
    }

    node& new_node = mPool.get(pool_index);
    new_node.create(mDevice);

    return {static_cast<handle::handle_t>(pool_index)};
}

void phi::d3d12::FencePool::free(phi::handle::fence fence)
{
    if (!fence.is_valid())
        return;

    mPool.get(fence._value).free();

    {
        auto lg = std::lock_guard(mMutex);
        mPool.release(fence._value);
    }
}

void phi::d3d12::FencePool::free(cc::span<const phi::handle::fence> fence_span)
{
    auto lg = std::lock_guard(mMutex);

    for (auto as : fence_span)
    {
        if (as.is_valid())
        {
            mPool.get(as._value).free();
            mPool.release(as._value);
        }
    }
}

void phi::d3d12::FencePool::initialize(ID3D12Device* device, unsigned max_num_fences, cc::allocator* static_alloc)
{
    CC_ASSERT(mDevice == nullptr && "double init");
    mDevice = device;
    mPool.initialize(max_num_fences, static_alloc);
}

void phi::d3d12::FencePool::destroy()
{
    if (mDevice != nullptr)
    {
        auto num_leaks = 0;
        mPool.iterate_allocated_nodes([&](node& leaked_fence) {
            ++num_leaks;
            leaked_fence.free();
        });

        if (num_leaks > 0)
        {
            PHI_LOG("leaked {} handle::fence object{}", num_leaks, num_leaks == 1 ? "" : "s");
        }
    }
}

void phi::d3d12::FencePool::signalCPU(phi::handle::fence fence, uint64_t new_val) const
{
    //
    internalGet(fence).fence->Signal(new_val);
}

void phi::d3d12::FencePool::signalGPU(phi::handle::fence fence, uint64_t new_val, ID3D12CommandQueue& queue) const
{
    //
    queue.Signal(internalGet(fence).fence, new_val);
}

void phi::d3d12::FencePool::waitCPU(phi::handle::fence fence, uint64_t val) const
{
    auto const& nd = internalGet(fence);
    if (nd.fence->GetCompletedValue() <= val)
    {
        PHI_D3D12_VERIFY(nd.fence->SetEventOnCompletion(val, nd.event));
        ::WaitForSingleObject(nd.event, INFINITE);
    }
}

void phi::d3d12::FencePool::waitGPU(phi::handle::fence fence, uint64_t val, ID3D12CommandQueue& queue) const
{
    //
    PHI_D3D12_VERIFY(queue.Wait(internalGet(fence).fence, val));
}

uint64_t phi::d3d12::FencePool::getValue(phi::handle::fence fence) const
{
    auto const& nd = internalGet(fence);
    auto const res = nd.fence->GetCompletedValue();
#ifdef CC_ENABLE_ASSERTIONS
    PHI_D3D12_DRED_ASSERT(res != UINT64_MAX, nd.fence);
#endif
    return res;
}

void phi::d3d12::FencePool::node::create(ID3D12Device* dev)
{
    PHI_D3D12_VERIFY(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    event = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
    CC_ASSERT(event != INVALID_HANDLE_VALUE && "failed to create win32 event");
}

void phi::d3d12::FencePool::node::free()
{
    fence->Release();
    ::CloseHandle(event);
}
