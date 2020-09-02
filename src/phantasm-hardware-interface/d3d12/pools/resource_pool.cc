#include "resource_pool.hh"

#include <clean-core/bit_cast.hh>
#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/detail/log.hh>

#include <phantasm-hardware-interface/d3d12/common/d3dx12.hh>
#include <phantasm-hardware-interface/d3d12/common/dxgi_format.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>
#include <phantasm-hardware-interface/d3d12/memory/D3D12MA.hh>

namespace
{
constexpr char const* d3d12_get_tex_dim_literal(phi::texture_dimension tdim)
{
    using td = phi::texture_dimension;
    switch (tdim)
    {
    case td::t1d:
        return "1d";
    case td::t2d:
        return "2d";
    case td::t3d:
        return "3d";
    }
    return "ud";
}

constexpr char const* d3d12_get_heap_type_literal(phi::resource_heap heap)
{
    switch (heap)
    {
    case phi::resource_heap::gpu:
        return "gpu";
    case phi::resource_heap::upload:
        return "upload";
    case phi::resource_heap::readback:
        return "readback";
    }

    return "unknown_heap_type";
}

constexpr D3D12_RESOURCE_STATES d3d12_get_initial_state_by_heap(phi::resource_heap heap)
{
    switch (heap)
    {
    case phi::resource_heap::gpu:
        return D3D12_RESOURCE_STATE_COMMON;
    case phi::resource_heap::upload:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case phi::resource_heap::readback:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    }

    return D3D12_RESOURCE_STATE_COMMON;
}
}

void phi::d3d12::ResourcePool::initialize(ID3D12Device* device, unsigned max_num_resources, unsigned max_num_swapchains)
{
    mAllocator.initialize(device);
    mPool.initialize(max_num_resources + max_num_swapchains); // additional resources for swapchain backbuffers

    mNumReservedBackbuffers = max_num_swapchains;
    for (auto i = 0u; i < mNumReservedBackbuffers; ++i)
    {
        auto backbuffer_reserved = mPool.acquire();
    }
}

void phi::d3d12::ResourcePool::destroy()
{
    for (auto i = 0u; i < mNumReservedBackbuffers; ++i)
    {
        mPool.release(mPool.unsafe_construct_handle_for_index(i));
    }

    auto num_leaks = 0;

    char debugname_buffer[256];
    mPool.iterate_allocated_nodes([&](resource_node& leaked_node) {
        if (leaked_node.allocation != nullptr)
        {
            if (num_leaks == 0)
                PHI_LOG("handle::resource leaks:");

            ++num_leaks;

            auto const strlen = util::get_object_name(leaked_node.resource, debugname_buffer);
            PHI_LOG("  leaked handle::resource - {}", cc::string_view(debugname_buffer, cc::min<UINT>(strlen, sizeof(debugname_buffer))));

            leaked_node.allocation->Release();
        }
    });

    if (num_leaks > 0)
    {
        PHI_LOG("leaked {} handle::resource object{}", num_leaks, (num_leaks == 1 ? "" : "s"));
    }

    mAllocator.destroy();
}

phi::handle::resource phi::d3d12::ResourcePool::injectBackbufferResource(unsigned swapchain_index, ID3D12Resource* raw_resource, D3D12_RESOURCE_STATES state)
{
    CC_ASSERT(swapchain_index < mNumReservedBackbuffers && "swapchain index OOB");
    auto const res_handle = mPool.unsafe_construct_handle_for_index(swapchain_index);
    resource_node& backbuffer_node = mPool.get(res_handle);
    backbuffer_node.type = resource_node::resource_type::image;
    backbuffer_node.resource = raw_resource;
    backbuffer_node.master_state = state;
    return {res_handle};
}

phi::handle::resource phi::d3d12::ResourcePool::createTexture(
    format format, unsigned w, unsigned h, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav, const char* dbg_name)
{
    constexpr D3D12_RESOURCE_STATES initial_state = util::to_native(resource_state::copy_dest);

    CC_CONTRACT(w > 0 && h > 0);

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = util::to_native(dim);
    desc.Format = util::to_dxgi_format(format);
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = UINT16(depth_or_array_size);
    desc.MipLevels = UINT16(mips);
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE; // NOTE: more?
    if (allow_uav)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Alignment = 0;

    auto* const alloc = mAllocator.allocate(desc, initial_state);
    auto const real_mip_levels = alloc->GetResource()->GetDesc().MipLevels;
    util::set_object_name(alloc->GetResource(), "pool tex%s[%u] %s (%ux%u, %u mips)", d3d12_get_tex_dim_literal(dim), depth_or_array_size,
                          dbg_name ? dbg_name : "", w, h, real_mip_levels);
    return acquireImage(alloc, format, initial_state, real_mip_levels, desc.DepthOrArraySize);
}

phi::handle::resource phi::d3d12::ResourcePool::createRenderTarget(
    phi::format format, unsigned w, unsigned h, unsigned samples, unsigned array_size, rt_clear_value const* optimized_clear_val, const char* dbg_name)
{
    CC_CONTRACT(w > 0 && h > 0);

    auto const format_dxgi = util::to_dxgi_format(format);
    if (is_depth_format(format))
    {
        // Depth-stencil target
        constexpr D3D12_RESOURCE_STATES initial_state = util::to_native(resource_state::depth_write);

        D3D12_CLEAR_VALUE clear_value;
        clear_value.Format = format_dxgi;
        if (optimized_clear_val)
        {
            clear_value.DepthStencil.Depth = optimized_clear_val->red_or_depth / 255.f;
            clear_value.DepthStencil.Stencil = optimized_clear_val->green_or_stencil;
        }
        else
        {
            clear_value.DepthStencil.Depth = 1;
            clear_value.DepthStencil.Stencil = 0;
        }

        auto const desc = CD3DX12_RESOURCE_DESC::Tex2D(format_dxgi, w, h, UINT16(array_size), 1, samples,
                                                       samples != 1 ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        auto* const alloc = mAllocator.allocate(desc, initial_state, &clear_value);
        util::set_object_name(alloc->GetResource(), "pool depth tgt %s (%ux%u, fmt %u)", dbg_name ? dbg_name : "", w, h, unsigned(format));
        return acquireImage(alloc, format, initial_state, desc.MipLevels, desc.ArraySize());
    }
    else
    {
        // Render target
        constexpr D3D12_RESOURCE_STATES initial_state = util::to_native(resource_state::render_target);

        D3D12_CLEAR_VALUE clear_value;
        clear_value.Format = format_dxgi;
        if (optimized_clear_val)
        {
            clear_value.Color[0] = optimized_clear_val->red_or_depth / 255.f;
            clear_value.Color[1] = optimized_clear_val->green_or_stencil / 255.f;
            clear_value.Color[2] = optimized_clear_val->blue / 255.f;
            clear_value.Color[3] = optimized_clear_val->alpha / 255.f;
        }
        else
        {
            clear_value.Color[0] = 0.0f;
            clear_value.Color[1] = 0.0f;
            clear_value.Color[2] = 0.0f;
            clear_value.Color[3] = 1.0f;
        }

        auto const desc = CD3DX12_RESOURCE_DESC::Tex2D(format_dxgi, w, h, UINT16(array_size), 1, samples,
                                                       samples != 1 ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        auto* const alloc = mAllocator.allocate(desc, initial_state, &clear_value);
        util::set_object_name(alloc->GetResource(), "pool render tgt %s (%ux%u, fmt %u)", dbg_name ? dbg_name : "", w, h, unsigned(format));
        return acquireImage(alloc, format, initial_state, desc.MipLevels, desc.ArraySize());
    }
}

phi::handle::resource phi::d3d12::ResourcePool::createBuffer(uint64_t size_bytes, unsigned stride_bytes, resource_heap heap, bool allow_uav, const char* dbg_name)
{
    CC_CONTRACT(size_bytes > 0);
    D3D12_RESOURCE_STATES const initial_state = d3d12_get_initial_state_by_heap(heap);

    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size_bytes);

    if (allow_uav)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto* const alloc = mAllocator.allocate(desc, initial_state, nullptr, util::to_native(heap));
    util::set_object_name(alloc->GetResource(), "pool buf %s (%uB, %uB stride, %s heap)", dbg_name ? dbg_name : "", unsigned(size_bytes),
                          stride_bytes, d3d12_get_heap_type_literal(heap));
    return acquireBuffer(alloc, initial_state, size_bytes, stride_bytes, heap);
}

std::byte* phi::d3d12::ResourcePool::mapBuffer(phi::handle::resource res)
{
    CC_ASSERT(res.is_valid() && "attempted to map invalid handle");

    resource_node const& node = mPool.get(unsigned(res._value));

    CC_ASSERT(node.type == resource_node::resource_type::buffer && node.heap != resource_heap::gpu && //
              "attempted to map non-buffer or buffer on GPU heap");


    D3D12_RANGE range = {0, node.buffer.width};
    void* data_start_void;
    PHI_D3D12_VERIFY(node.resource->Map(0, &range, &data_start_void));
    return cc::bit_cast<std::byte*>(data_start_void);
}

void phi::d3d12::ResourcePool::unmapBuffer(phi::handle::resource res)
{
    CC_ASSERT(res.is_valid() && "attempted to unmap invalid handle");

    resource_node const& node = mPool.get(unsigned(res._value));

    CC_ASSERT(node.type == resource_node::resource_type::buffer && node.heap != resource_heap::gpu && //
              "attempted to unmap non-buffer or buffer on GPU heap");

    D3D12_RANGE range = {0, node.heap == resource_heap::readback ? 0 : node.buffer.width};
    node.resource->Unmap(0, &range);
}

phi::handle::resource phi::d3d12::ResourcePool::createBufferInternal(uint64_t size_bytes, unsigned stride_bytes, bool allow_uav, D3D12_RESOURCE_STATES initial_state)
{
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size_bytes);

    if (allow_uav)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto* const alloc = mAllocator.allocate(desc, initial_state);
    util::set_object_name(alloc->GetResource(), "respool internal buffer");
    return acquireBuffer(alloc, initial_state, size_bytes, stride_bytes, resource_heap::gpu);
}

void phi::d3d12::ResourcePool::free(phi::handle::resource res)
{
    if (!res.is_valid())
        return;
    CC_ASSERT(!isBackbuffer(res) && "the backbuffer resource must not be freed");

    // This requires no synchronization, as D3D12MA internally syncs
    resource_node& freed_node = mPool.get(unsigned(res._value));
    freed_node.allocation->Release();

    {
        // This is a write access to the pool and must be synced
        auto lg = std::lock_guard(mMutex);
        mPool.release(unsigned(res._value));
    }
}

void phi::d3d12::ResourcePool::free(cc::span<const phi::handle::resource> resources)
{
    auto lg = std::lock_guard(mMutex);

    for (auto res : resources)
    {
        if (res.is_valid())
        {
            CC_ASSERT(!isBackbuffer(res) && "the backbuffer resource must not be freed");
            resource_node& freed_node = mPool.get(unsigned(res._value));
            // This is a write access to mAllocatorDescriptors
            freed_node.allocation->Release();
            // This is a write access to the pool and must be synced
            mPool.release(unsigned(res._value));
        }
    }
}

void phi::d3d12::ResourcePool::setDebugName(phi::handle::resource res, const char* name, unsigned name_length)
{
    CC_CONTRACT(name != nullptr);
    util::set_object_name(internalGet(res).resource, "%*s [respool named]", name_length, name);
}

phi::handle::resource phi::d3d12::ResourcePool::acquireBuffer(
    D3D12MA::Allocation* alloc, D3D12_RESOURCE_STATES initial_state, uint64_t buffer_width, unsigned buffer_stride, phi::resource_heap heap)
{
    unsigned res;
    {
        // This is a write access to the pool and must be synced
        auto lg = std::lock_guard(mMutex);
        res = mPool.acquire();
    }
    resource_node& new_node = mPool.get(res);
    new_node.allocation = alloc;
    new_node.resource = alloc->GetResource();
    new_node.type = resource_node::resource_type::buffer;
    new_node.heap = heap;
    new_node.master_state = initial_state;
    new_node.buffer.width = unsigned(buffer_width);
    new_node.buffer.stride = buffer_stride;

    return {static_cast<handle::handle_t>(res)};
}

phi::handle::resource phi::d3d12::ResourcePool::acquireImage(
    D3D12MA::Allocation* alloc, phi::format pixel_format, D3D12_RESOURCE_STATES initial_state, unsigned num_mips, unsigned num_array_layers)
{
    unsigned res;
    {
        // This is a write access to the pool and must be synced
        auto lg = std::lock_guard(mMutex);
        res = mPool.acquire();
    }

    resource_node& new_node = mPool.get(res);
    new_node.allocation = alloc;
    new_node.resource = alloc->GetResource();
    new_node.type = resource_node::resource_type::image;
    new_node.heap = resource_heap::gpu;
    new_node.master_state = initial_state;
    new_node.image.num_mips = num_mips;
    new_node.image.num_array_layers = num_array_layers;
    new_node.image.pixel_format = pixel_format;

    return {static_cast<handle::handle_t>(res)};
}
