#include "Swapchain.hh"

#include "common/util.hh"
#include "common/verify.hh"

namespace
{
// NOTE: The _SRGB variant crashes at factory.CreateSwapChainForHwnd
constexpr auto gc_backbuffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;

DXGI_SWAP_CHAIN_FLAG get_swapchain_flags(phi::present_mode mode)
{
    return mode == phi::present_mode::allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : DXGI_SWAP_CHAIN_FLAG(0);
}
}

void phi::d3d12::Swapchain::initialize(
    IDXGIFactory4& factory, shared_com_ptr<ID3D12Device> device, shared_com_ptr<ID3D12CommandQueue> queue, HWND handle, unsigned num_backbuffers, present_mode present_mode)
{
    CC_RUNTIME_ASSERT(num_backbuffers <= max_num_backbuffers);
    mBackbuffers.emplace(num_backbuffers);
    mParentDevice = cc::move(device);
    mParentDirectQueue = cc::move(queue);
    mPresentMode = present_mode;

    // Create fences
    {
        for (auto i = 0u; i < mBackbuffers.size(); ++i)
        {
            mBackbuffers[i].fence.initialize(*mParentDevice);
            util::set_object_name(mBackbuffers[i].fence.getRawFence(), "swapchain fence #%u", i);
        }
    }

    // Create swapchain
    {
        // Swapchains are always using FLIP_DISCARD and allow tearing depending on the settings
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
        swapchain_desc.BufferCount = num_backbuffers;
        swapchain_desc.Width = 0;
        swapchain_desc.Height = 0;
        swapchain_desc.Format = gc_backbuffer_format;
        swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchain_desc.SampleDesc.Count = 1;
        swapchain_desc.Flags = get_swapchain_flags(mPresentMode);

        shared_com_ptr<IDXGISwapChain1> temp_swapchain;
        PHI_D3D12_VERIFY(factory.CreateSwapChainForHwnd(mParentDirectQueue, handle, &swapchain_desc, nullptr, nullptr, temp_swapchain.override()));
        PHI_D3D12_VERIFY(temp_swapchain.get_interface(mSwapchain));
    }

    // Disable Alt + Enter behavior
    PHI_D3D12_VERIFY(factory.MakeWindowAssociation(handle, DXGI_MWA_NO_WINDOW_CHANGES));

    // Create backbuffer RTV heap, then create RTVs
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
        rtv_heap_desc.NumDescriptors = num_backbuffers;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtv_heap_desc.NodeMask = 0;

        PHI_D3D12_VERIFY(mParentDevice->CreateDescriptorHeap(&rtv_heap_desc, PHI_COM_WRITE(mRTVHeap)));
        util::set_object_name(mRTVHeap, "swapchain RTV heap");

        updateBackbuffers();
    }
}

phi::d3d12::Swapchain::~Swapchain() { releaseBackbuffers(); }

void phi::d3d12::Swapchain::onResize(tg::isize2 size)
{
    mBackbufferSize = size;
    releaseBackbuffers();
    PHI_D3D12_VERIFY(mSwapchain->ResizeBuffers(unsigned(mBackbuffers.size()), UINT(mBackbufferSize.width), UINT(mBackbufferSize.height),
                                              gc_backbuffer_format, get_swapchain_flags(mPresentMode)));
    updateBackbuffers();
}

void phi::d3d12::Swapchain::setFullscreen(bool fullscreen) { PHI_D3D12_VERIFY(mSwapchain->SetFullscreenState(fullscreen, nullptr)); }

void phi::d3d12::Swapchain::present()
{
    PHI_D3D12_VERIFY_FULL(mSwapchain->Present(0, mPresentMode == present_mode::allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0), mParentDevice);

    auto const backbuffer_i = mSwapchain->GetCurrentBackBufferIndex();
    mBackbuffers[backbuffer_i].fence.issueFence(*mParentDirectQueue);
}

unsigned phi::d3d12::Swapchain::waitForBackbuffer()
{
    auto const backbuffer_i = mSwapchain->GetCurrentBackBufferIndex();
    mBackbuffers[backbuffer_i].fence.waitOnCPU(0);
    return backbuffer_i;
}

DXGI_FORMAT phi::d3d12::Swapchain::getBackbufferFormat() const { return gc_backbuffer_format; }

void phi::d3d12::Swapchain::updateBackbuffers()
{
    auto const rtv_size = mParentDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (auto i = 0u; i < mBackbuffers.size(); ++i)
    {
        auto& backbuffer = mBackbuffers[i];

        backbuffer.state = D3D12_RESOURCE_STATE_PRESENT;

        backbuffer.rtv = mRTVHeap->GetCPUDescriptorHandleForHeapStart();
        backbuffer.rtv.ptr += rtv_size * i;

        PHI_D3D12_VERIFY(mSwapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffer.resource)));
        util::set_object_name(backbuffer.resource, "swapchain backbuffer #%u", i);

        mParentDevice->CreateRenderTargetView(backbuffer.resource, nullptr, backbuffer.rtv);

        // backbuffer.resource->Release();
        // Usually, this call would be reasonable, removing the need for manual management down the line
        // But there is a known deadlock in the D3D12 validation layer which occurs if the backbuffers are unreferenced
        // Instead we must release backbuffers before resizes and at destruction (see ::releaseBackbuffers)
    }
}

void phi::d3d12::Swapchain::releaseBackbuffers()
{
    // This method is a workaround for a known deadlock in the D3D12 validation layer
    for (auto& backbuffer : mBackbuffers)
    {
        backbuffer.resource->Release();
    }
}
