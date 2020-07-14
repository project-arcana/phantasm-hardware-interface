#include "swapchain_pool.hh"

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/detail/log.hh>

namespace
{
// NOTE: The _SRGB variant crashes at factory.CreateSwapChainForHwnd
constexpr auto gc_pool_backbuffer_format = DXGI_FORMAT_B8G8R8A8_UNORM;

DXGI_SWAP_CHAIN_FLAG get_pool_swapchain_flags(phi::present_mode mode)
{
    return mode == phi::present_mode::allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : DXGI_SWAP_CHAIN_FLAG(0);
}
}

void phi::d3d12::SwapchainPool::initialize(IDXGIFactory4* factory, ID3D12Device* device, ID3D12CommandQueue* queue, unsigned max_num_swapchains)
{
    CC_ASSERT(mParentFactory == nullptr && "double init");
    mParentFactory = factory;
    mParentDevice = device;
    mParentQueue = queue;

    mPool.initialize(max_num_swapchains);

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
}

void phi::d3d12::SwapchainPool::destroy()
{
    if (mParentFactory != nullptr)
    {
        unsigned num_leaks = 0;
        mPool.iterate_allocated_nodes([&](swapchain& node) {
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

phi::handle::swapchain phi::d3d12::SwapchainPool::createSwapchain(HWND window_handle, int initial_w, int initial_h, unsigned num_backbuffers, phi::present_mode mode)
{
    handle::index_t res;
    {
        auto lg = std::lock_guard(mMutex);
        res = mPool.acquire();
    }

    swapchain& new_node = mPool.get(res);

    new_node.backbuf_width = initial_w;
    new_node.backbuf_height = initial_h;
    new_node.mode = mode;
    new_node.has_resized = false;
    CC_ASSERT(num_backbuffers < 6 && "too many backbuffers configured");
    new_node.backbuffers.emplace(num_backbuffers);

    // Create fences
    for (auto i = 0u; i < new_node.backbuffers.size(); ++i)
    {
        new_node.backbuffers[i].fence.initialize(*mParentDevice);
        util::set_object_name(new_node.backbuffers[i].fence.getRawFence(), "swapchain %u - fence #%u", mPool.get_handle_index(res), i);
    }

    // create swapchain
    {
        // Swapchains are always using FLIP_DISCARD and allow tearing depending on the settings
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
        swapchain_desc.BufferCount = num_backbuffers;
        swapchain_desc.Width = UINT(initial_w);
        swapchain_desc.Height = UINT(initial_h);
        swapchain_desc.Format = gc_pool_backbuffer_format;
        swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchain_desc.SampleDesc.Count = 1;
        swapchain_desc.Flags = get_pool_swapchain_flags(mode);

        shared_com_ptr<IDXGISwapChain1> temp_swapchain;
        PHI_D3D12_VERIFY(mParentFactory->CreateSwapChainForHwnd(mParentQueue, window_handle, &swapchain_desc, nullptr, nullptr, temp_swapchain.override()));
        PHI_D3D12_VERIFY(temp_swapchain->QueryInterface(IID_PPV_ARGS(&new_node.swapchain_com)));
    }

    // Disable Alt + Enter behavior
    PHI_D3D12_VERIFY(mParentFactory->MakeWindowAssociation(window_handle, DXGI_MWA_NO_WINDOW_CHANGES));

    // Create backbuffer RTVs
    auto const res_handle = handle::swapchain{res};
    updateBackbuffers(res_handle);

    return res_handle;
}

void phi::d3d12::SwapchainPool::free(phi::handle::swapchain handle)
{
    // This requires no synchronization, as D3D12MA internally syncs
    swapchain& freed_node = mPool.get(handle._value);
    internalFree(freed_node);

    {
        // This is a write access to the pool and must be synced
        auto lg = std::lock_guard(mMutex);
        mPool.release(handle._value);
    }
}

void phi::d3d12::SwapchainPool::onResize(phi::handle::swapchain handle, int w, int h)
{
    swapchain& node = mPool.get(handle._value);
    node.backbuf_width = w;
    node.backbuf_height = h;
    node.has_resized = true;
    releaseBackbuffers(node);
    PHI_D3D12_VERIFY(node.swapchain_com->ResizeBuffers(unsigned(node.backbuffers.size()), UINT(w), UINT(h), gc_pool_backbuffer_format,
                                                       get_pool_swapchain_flags(node.mode)));
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
    PHI_D3D12_VERIFY_FULL(node.swapchain_com->Present(0, node.mode == present_mode::allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0), mParentDevice);

    auto const backbuffer_i = node.swapchain_com->GetCurrentBackBufferIndex();
    node.backbuffers[backbuffer_i].fence.issueFence(*mParentQueue);
}

unsigned phi::d3d12::SwapchainPool::waitForBackbuffer(phi::handle::swapchain handle)
{
    swapchain& node = mPool.get(handle._value);
    auto const backbuffer_i = node.swapchain_com->GetCurrentBackBufferIndex();
    node.backbuffers[backbuffer_i].fence.waitOnCPU(0);
    return backbuffer_i;
}

DXGI_FORMAT phi::d3d12::SwapchainPool::getBackbufferFormat() const { return gc_pool_backbuffer_format; }

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
        util::set_object_name(backbuffer.resource, "swapchain %u backbuffer #%u", swapchain_index, i);

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