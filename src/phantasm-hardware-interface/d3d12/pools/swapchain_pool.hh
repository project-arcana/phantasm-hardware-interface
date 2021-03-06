#pragma once

#include <clean-core/atomic_linked_pool.hh>
#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/handles.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>

#include <phantasm-hardware-interface/d3d12/Fence.hh>

namespace phi::d3d12
{
class SwapchainPool
{
public:
    struct backbuffer
    {
        Fence fence;                     // present fence - GPU signalled on present, CPU waited on acquire
        D3D12_CPU_DESCRIPTOR_HANDLE rtv; // CPU RTV
        ID3D12Resource* resource;        // resource ptr
        D3D12_RESOURCE_STATES state;     // current state
    };

    struct swapchain
    {
        IDXGISwapChain3* swapchain_com; // Swapchain COM Ptr
        int backbuf_width;
        int backbuf_height;
        present_mode mode;
        bool has_resized;
        cc::capped_vector<backbuffer, 6> backbuffers; // all backbuffers
        uint32_t last_acquired_backbuf_i = 0;
    };

public:
    handle::swapchain createSwapchain(HWND window_handle, int initial_w, int initial_h, unsigned num_backbuffers, present_mode mode);

    void free(handle::swapchain handle);

    void onResize(handle::swapchain handle, int w, int h);

    bool clearResizeFlag(handle::swapchain handle)
    {
        auto& node = mPool.get(handle._value);
        if (!node.has_resized)
            return false;

        node.has_resized = false;
        return true;
    }

    void setFullscreen(handle::swapchain handle, bool fullscreen);

    void present(handle::swapchain handle);

    unsigned acquireBackbuffer(handle::swapchain handle);

    swapchain const& get(handle::swapchain handle) const { return mPool.get(handle._value); }

    unsigned getSwapchainIndex(handle::swapchain handle) const { return mPool.get_handle_index(handle._value); }

    DXGI_FORMAT getBackbufferFormat() const;

public:
    void initialize(IDXGIFactory4* factory, ID3D12Device* device, ID3D12CommandQueue* queue, unsigned max_num_swapchains, cc::allocator* static_alloc);
    void destroy();


private:
    void updateBackbuffers(handle::swapchain handle);

    void releaseBackbuffers(swapchain& node);

    void internalFree(swapchain& node);

private:
    // nonowning
    IDXGIFactory4* mParentFactory = nullptr;    ///< The parent adapter's factory
    ID3D12Device* mParentDevice = nullptr;      ///< The device
    ID3D12CommandQueue* mParentQueue = nullptr; ///< The device's queue being used to present

    // owning
    cc::atomic_linked_pool<swapchain> mPool;
    ID3D12DescriptorHeap* mRTVHeap;
    UINT mRTVSize;
};
}
