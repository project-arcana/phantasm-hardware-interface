#include "swapchain_pool.hh"

#ifdef PHI_HAS_OPTICK
#include <optick.h>
#endif

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/common/format_info_list.hh>
#include <phantasm-hardware-interface/common/format_size.hh>
#include <phantasm-hardware-interface/common/log.hh>

#include <phantasm-hardware-interface/d3d12/common/shared_com_ptr.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

namespace
{
unsigned get_sync_interval(phi::present_mode mode)
{
    switch (mode)
    {
    case phi::present_mode::synced:
        return 1;
    case phi::present_mode::synced_2nd_vblank:
        return 2;
    case phi::present_mode::unsynced:
    case phi::present_mode::unsynced_allow_tearing:
    default:
        return 0;
    }
}
} // namespace

void phi::d3d12::SwapchainPool::initialize(IDXGIFactory6* factory, ID3D12Device* device, ID3D12CommandQueue* queue, unsigned max_num_swapchains, cc::allocator* static_alloc)
{
    CC_ASSERT(mParentFactory == nullptr && "double init");
    mParentFactory = factory;
    mParentDevice = device;
    mParentQueue = queue;

    mPool.initialize(max_num_swapchains, static_alloc);

    // create dedicated RTV heap for backbuffer RTVs
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
        rtv_heap_desc.NumDescriptors = max_num_swapchains * 6;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtv_heap_desc.NodeMask = 0;

        PHI_D3D12_VERIFY(mParentDevice->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&mRTVHeap)));
        util::set_object_name(mRTVHeap, "swapchain pool backbuffer RTV heap");

        mRTVSize = mParentDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // test for tearing support
    {
        BOOL bTearingSupported = FALSE;
        HRESULT hr = mParentFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &bTearingSupported, sizeof(mTearingSupported));

        mTearingSupported = SUCCEEDED(hr) && bTearingSupported;
    }
}

void phi::d3d12::SwapchainPool::destroy()
{
    if (mParentFactory != nullptr)
    {
        unsigned num_leaks = 0;
        mPool.iterate_allocated_nodes(
            [&](swapchain& node)
            {
                internalFree(node);
                ++num_leaks;
            });

        if (num_leaks > 0)
        {
            PHI_LOG("leaked {} handle::swapchain object{}", num_leaks, (num_leaks == 1 ? "" : "s"));
        }

        mPool.destroy();
        mRTVHeap->Release();
    }
}

phi::handle::swapchain phi::d3d12::SwapchainPool::createSwapchain(HWND window_handle, arg::swapchain_description const& desc, char const* pDebugName)
{
    CC_CONTRACT(desc.initial_width > 0 && desc.initial_height > 0);
    handle::handle_t const res = mPool.acquire();

    phi::format const effectiveFormat = desc.format_preference == phi::format::none ? phi::format::bgra8un : desc.format_preference;

    swapchain& new_node = mPool.get(res);
    new_node.backbuf_width = desc.initial_width;
    new_node.backbuf_height = desc.initial_height;
    new_node.fmt = util::to_dxgi_format(effectiveFormat);
    new_node.mode = desc.mode;
    new_node.has_resized = false;
    CC_ASSERT(desc.num_backbuffers < 6 && "too many backbuffers configured");
    new_node.backbuffers.resize(desc.num_backbuffers);

    snprintf(new_node.debugname, sizeof(new_node.debugname), "%s", pDebugName ? pDebugName : "(Unnamed)");

    // Create fences
    for (auto i = 0u; i < new_node.backbuffers.size(); ++i)
    {
        new_node.backbuffers[i].fence.initialize(*mParentDevice);
        util::set_object_name(new_node.backbuffers[i].fence.getRawFence(), "swapchain %s - fence #%u", new_node.debugname, i);
    }

    // create swapchain
    {
        // Swapchains are always using FLIP_DISCARD and allow tearing depending on the settings
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
        swapchain_desc.BufferCount = desc.num_backbuffers;
        swapchain_desc.Width = UINT(desc.initial_width);
        swapchain_desc.Height = UINT(desc.initial_height);
        swapchain_desc.Format = new_node.fmt;
        swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchain_desc.SampleDesc.Count = 1;
        swapchain_desc.Flags = mTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        shared_com_ptr<IDXGISwapChain1> temp_swapchain;
        PHI_D3D12_VERIFY(mParentFactory->CreateSwapChainForHwnd(mParentQueue, window_handle, &swapchain_desc, nullptr, nullptr, temp_swapchain.override()));
        PHI_D3D12_VERIFY(temp_swapchain->QueryInterface(IID_PPV_ARGS(&new_node.swapchain_com)));

        util::set_object_name(new_node.swapchain_com, "PHI Swapchain %s", new_node.debugname);
    }

    // Disable Alt + Enter behavior
    PHI_D3D12_VERIFY(mParentFactory->MakeWindowAssociation(window_handle, DXGI_MWA_NO_ALT_ENTER));

    // HDR settings
    if (desc.enable_hdr)
    {
        bool bIsRec2020 = false;
        DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

        if (effectiveFormat == format::bgra8un || effectiveFormat == format::rgba8un)
        {
            // sRGB
            PHI_LOG_WARN("Created a HDR swapchain with <= 8 bit per color channel, disabling HDR");
        }
        if (effectiveFormat == phi::format::r10g10b10a2un || effectiveFormat == phi::format::r10g10b10a2u)
        {
            // bEnableST2048 = true;
            bIsRec2020 = true;
            colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        }
        else if (effectiveFormat == phi::format::rgba16f)
        {
            // linear
            // bEnableST2048 = false;
            colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        }
        else
        {
            PHI_LOG_WARN("Created a HDR swapchain with unknown format");
        }

        UINT colorSpaceSupport = 0;
        HRESULT hr = new_node.swapchain_com->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport);
        if (SUCCEEDED(hr) && (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)
        {
            PHI_D3D12_VERIFY(new_node.swapchain_com->SetColorSpace1(colorSpace));

            // From D3D12HDR sample, (MIT)
            struct DisplayChromaticities
            {
                float RedX;
                float RedY;
                float GreenX;
                float GreenY;
                float BlueX;
                float BlueY;
                float WhiteX;
                float WhiteY;
            };
            static const DisplayChromaticities DisplayChromaticityList[] = {
                {0.64000f, 0.33000f, 0.30000f, 0.60000f, 0.15000f, 0.06000f, 0.31270f, 0.32900f}, // Display Gamut Rec709
                {0.70800f, 0.29200f, 0.17000f, 0.79700f, 0.13100f, 0.04600f, 0.31270f, 0.32900f}, // Display Gamut Rec2020
            };

            // Set HDR meta data
            const DisplayChromaticities& Chroma = DisplayChromaticityList[bIsRec2020 ? 1 : 0];
            DXGI_HDR_METADATA_HDR10 HDR10MetaData = {};
            HDR10MetaData.RedPrimary[0] = static_cast<UINT16>(Chroma.RedX * 50000.0f);
            HDR10MetaData.RedPrimary[1] = static_cast<UINT16>(Chroma.RedY * 50000.0f);
            HDR10MetaData.GreenPrimary[0] = static_cast<UINT16>(Chroma.GreenX * 50000.0f);
            HDR10MetaData.GreenPrimary[1] = static_cast<UINT16>(Chroma.GreenY * 50000.0f);
            HDR10MetaData.BluePrimary[0] = static_cast<UINT16>(Chroma.BlueX * 50000.0f);
            HDR10MetaData.BluePrimary[1] = static_cast<UINT16>(Chroma.BlueY * 50000.0f);
            HDR10MetaData.WhitePoint[0] = static_cast<UINT16>(Chroma.WhiteX * 50000.0f);
            HDR10MetaData.WhitePoint[1] = static_cast<UINT16>(Chroma.WhiteY * 50000.0f);
            HDR10MetaData.MaxMasteringLuminance = static_cast<UINT>(desc.hdr_max_output_nits * 10000.0f);
            HDR10MetaData.MinMasteringLuminance = static_cast<UINT>(desc.hdr_min_output_nits * 10000.0f);
            HDR10MetaData.MaxContentLightLevel = static_cast<UINT16>(desc.hdr_max_content_light_level);
            HDR10MetaData.MaxFrameAverageLightLevel = static_cast<UINT16>(desc.hdr_max_frame_average_light_level);
            PHI_D3D12_VERIFY(new_node.swapchain_com->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &HDR10MetaData));
        }
        else
        {
            PHI_LOG_ERROR("HDR swapchain color space unsupported");
        }
    }

    // Create backbuffer RTVs
    auto const res_handle = handle::swapchain{res};
    updateBackbuffers(res_handle);

    return res_handle;
}

void phi::d3d12::SwapchainPool::free(phi::handle::swapchain handle)
{
    swapchain& freed_node = mPool.get(handle._value);
    internalFree(freed_node);

    mPool.release(handle._value);
}

void phi::d3d12::SwapchainPool::onResize(phi::handle::swapchain handle, int w, int h)
{
    CC_CONTRACT(w > 0 && h > 0);
    swapchain& node = mPool.get(handle._value);
    node.backbuf_width = w;
    node.backbuf_height = h;
    node.has_resized = true;
    releaseBackbuffers(node);
    PHI_D3D12_VERIFY(node.swapchain_com->ResizeBuffers(unsigned(node.backbuffers.size()), UINT(w), UINT(h), node.fmt,
                                                       mTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0));
    updateBackbuffers(handle);
}

void phi::d3d12::SwapchainPool::setFullscreen(phi::handle::swapchain handle, bool fullscreen)
{
    swapchain& node = mPool.get(handle._value);
    PHI_D3D12_VERIFY(node.swapchain_com->SetFullscreenState(fullscreen, nullptr));
}

void phi::d3d12::SwapchainPool::present(phi::handle::swapchain handle)
{
    swapchain& node = mPool.get(handle._value);

    // CPU-wait on currently acquired backbuffer
    node.backbuffers[node.last_acquired_backbuf_i].fence.waitOnCPU(0);

#ifdef PHI_HAS_OPTICK
    OPTICK_GPU_FLIP(node.swapchain_com);
#endif

    // present
    UINT const sync_interval = get_sync_interval(node.mode);
    UINT const flags = mTearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0;
    PHI_D3D12_VERIFY_FULL(node.swapchain_com->Present(sync_interval, flags), mParentDevice);

    // issue present fence on GPU for the next backbuffer in line
    auto const backbuffer_i = node.swapchain_com->GetCurrentBackBufferIndex();
    node.backbuffers[backbuffer_i].fence.issueFence(*mParentQueue);
}

unsigned phi::d3d12::SwapchainPool::acquireBackbuffer(phi::handle::swapchain handle)
{
    swapchain& node = mPool.get(handle._value);
    node.last_acquired_backbuf_i = node.swapchain_com->GetCurrentBackBufferIndex();
    return node.last_acquired_backbuf_i;
}

void phi::d3d12::SwapchainPool::updateBackbuffers(phi::handle::swapchain handle)
{
    unsigned const swapchain_index = mPool.get_handle_index(handle._value);
    swapchain& node = mPool.get(handle._value);

    for (auto i = 0u; i < node.backbuffers.size(); ++i)
    {
        auto& backbuffer = node.backbuffers[i];

        backbuffer.state = D3D12_RESOURCE_STATE_PRESENT;

        backbuffer.rtv = mRTVHeap->GetCPUDescriptorHandleForHeapStart();
        backbuffer.rtv.ptr += mRTVSize * (swapchain_index * 6 + i);

        PHI_D3D12_VERIFY(node.swapchain_com->GetBuffer(i, IID_PPV_ARGS(&backbuffer.resource)));
        util::set_object_name(backbuffer.resource, "swapchain %s backbuffer #%u", node.debugname, i);

        mParentDevice->CreateRenderTargetView(backbuffer.resource, nullptr, backbuffer.rtv);

        // backbuffer.resource->Release();
        // Usually, this call would be reasonable, removing the need for manual management down the line
        // But there is a known deadlock in the D3D12 validation layer which occurs if the backbuffers are unreferenced
        // Instead we must release backbuffers before resizes and at destruction (see ::releaseBackbuffers)
    }
}

void phi::d3d12::SwapchainPool::releaseBackbuffers(swapchain& node)
{
    // This method is a workaround for a known deadlock in the D3D12 validation layer
    for (auto& backbuffer : node.backbuffers)
    {
        backbuffer.resource->Release();
    }
}

void phi::d3d12::SwapchainPool::internalFree(phi::d3d12::SwapchainPool::swapchain& node)
{
    releaseBackbuffers(node);

    for (auto& bb : node.backbuffers)
        bb.fence.destroy();

    node.swapchain_com->Release();
}
