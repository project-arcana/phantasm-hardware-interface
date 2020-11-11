#pragma once

#include <clean-core/atomic_linked_pool.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>

namespace phi::d3d12
{
class FencePool
{
public:
    [[nodiscard]] handle::fence createFence();

    void free(handle::fence fence);
    void free(cc::span<handle::fence const> fence_span);

public:
    void initialize(ID3D12Device* device, unsigned max_num_fences, cc::allocator* static_alloc);
    void destroy();

    ID3D12Fence* get(handle::fence fence) const { return internalGet(fence).fence; }

    void signalCPU(handle::fence fence, uint64_t new_val) const;
    void signalGPU(handle::fence fence, uint64_t new_val, ID3D12CommandQueue& queue) const;

    void waitCPU(handle::fence fence, uint64_t val) const;
    void waitGPU(handle::fence fence, uint64_t val, ID3D12CommandQueue& queue) const;

    [[nodiscard]] uint64_t getValue(handle::fence fence) const;

private:
    struct node
    {
        ID3D12Fence* fence;
        ::HANDLE event;

        void create(ID3D12Device* dev);
        void free();
    };

    [[nodiscard]] node& internalGet(handle::fence fence)
    {
        CC_ASSERT(fence.is_valid() && "invalid handle::fence");
        return mPool.get(fence._value);
    }

    [[nodiscard]] node const& internalGet(handle::fence fence) const
    {
        CC_ASSERT(fence.is_valid() && "invalid handle::fence");
        return mPool.get(fence._value);
    }

private:
    ID3D12Device* mDevice = nullptr;

    cc::atomic_linked_pool<node> mPool;
};

}
