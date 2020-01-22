#include "resource_pool.hh"

#include <iostream>

#include <clean-core/bit_cast.hh>

#include <phantasm-hardware-interface/d3d12/common/d3dx12.hh>
#include <phantasm-hardware-interface/d3d12/common/dxgi_format.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/memory/D3D12MA.hh>

namespace
{
constexpr char const* get_tex_dim_literal(phi::texture_dimension tdim)
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
}

void phi::d3d12::ResourcePool::initialize(ID3D12Device& device, unsigned max_num_resources)
{
    mAllocator.initialize(device);
    // TODO: think about reserved bits for dangle check, assert max_num < 2^free bits
    mPool.initialize(max_num_resources + 1); // 1 additional resource for the backbuffer
    mInjectedBackbufferResource = {static_cast<handle::index_t>(mPool.acquire())};
}

void phi::d3d12::ResourcePool::destroy()
{
    auto num_leaks = 0;
    mPool.iterate_allocated_nodes([&](resource_node& leaked_node, unsigned) {
        if (leaked_node.allocation != nullptr)
        {
            ++num_leaks;
            leaked_node.allocation->Release();
        }
    });

    if (num_leaks > 0)
    {
        std::cout << "[phi][d3d12] warning: leaked " << num_leaks << " handle::resource object" << (num_leaks == 1 ? "" : "s") << std::endl;
    }

    mAllocator.destroy();
}

phi::handle::resource phi::d3d12::ResourcePool::injectBackbufferResource(ID3D12Resource* raw_resource, phi::resource_state state)
{
    resource_node& backbuffer_node = mPool.get(static_cast<unsigned>(mInjectedBackbufferResource.index));
    backbuffer_node.type = resource_node::resource_type::image;
    backbuffer_node.resource = raw_resource;
    backbuffer_node.master_state = state;
    return mInjectedBackbufferResource;
}

phi::handle::resource phi::d3d12::ResourcePool::createTexture(
    format format, unsigned w, unsigned h, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav)
{
    constexpr auto initial_state = resource_state::copy_dest;

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

    auto* const alloc = mAllocator.allocate(desc, util::to_native(initial_state));
    auto const real_mip_levels = alloc->GetResource()->GetDesc().MipLevels;
    util::set_object_name(alloc->GetResource(), "respool texture%s[%u] m%u", get_tex_dim_literal(dim), depth_or_array_size, real_mip_levels);

    return acquireImage(alloc, format, initial_state, real_mip_levels, desc.DepthOrArraySize);
}

phi::handle::resource phi::d3d12::ResourcePool::createRenderTarget(phi::format format, unsigned w, unsigned h, unsigned samples)
{
    auto const format_dxgi = util::to_dxgi_format(format);
    if (is_depth_format(format))
    {
        // Depth-stencil target
        constexpr auto initial_state = resource_state::depth_write;

        D3D12_CLEAR_VALUE clear_value;
        clear_value.Format = format_dxgi;
        clear_value.DepthStencil.Depth = 1;
        clear_value.DepthStencil.Stencil = 0;

        auto const desc = CD3DX12_RESOURCE_DESC::Tex2D(format_dxgi, w, h, 1, 1, samples, samples != 1 ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0,
                                                       D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        auto* const alloc = mAllocator.allocate(desc, util::to_native(initial_state), &clear_value);
        util::set_object_name(alloc->GetResource(), "respool depth stencil target");
        return acquireImage(alloc, format, initial_state, desc.MipLevels, desc.ArraySize());
    }
    else
    {
        // Render target
        constexpr auto initial_state = resource_state::render_target;

        D3D12_CLEAR_VALUE clear_value;
        clear_value.Format = format_dxgi;
        clear_value.Color[0] = 0.0f;
        clear_value.Color[1] = 0.0f;
        clear_value.Color[2] = 0.0f;
        clear_value.Color[3] = 1.0f;

        auto const desc = CD3DX12_RESOURCE_DESC::Tex2D(format_dxgi, UINT(w), UINT(h), 1, 1, UINT(samples),
                                                       samples != 1 ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        auto* const alloc = mAllocator.allocate(desc, util::to_native(initial_state), &clear_value);
        util::set_object_name(alloc->GetResource(), "respool render target");
        return acquireImage(alloc, format, initial_state, desc.MipLevels, desc.ArraySize());
    }
}

phi::handle::resource phi::d3d12::ResourcePool::createBuffer(uint64_t size_bytes, unsigned stride_bytes, bool allow_uav)
{
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size_bytes);

    if (allow_uav)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto* const alloc = mAllocator.allocate(desc, D3D12_RESOURCE_STATE_COMMON);
    util::set_object_name(alloc->GetResource(), "respool buffer");
    return acquireBuffer(alloc, resource_state::undefined, size_bytes, stride_bytes);
}

phi::handle::resource phi::d3d12::ResourcePool::createBufferInternal(uint64_t size_bytes, unsigned stride_bytes, bool allow_uav, D3D12_RESOURCE_STATES initial_state)
{
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size_bytes);

    if (allow_uav)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto* const alloc = mAllocator.allocate(desc, initial_state);
    util::set_object_name(alloc->GetResource(), "respool internal buffer");
    return acquireBuffer(alloc, resource_state::unknown, size_bytes, stride_bytes);
}

phi::handle::resource phi::d3d12::ResourcePool::createMappedBuffer(uint64_t size_bytes, unsigned stride_bytes)
{
    auto const desc = CD3DX12_RESOURCE_DESC::Buffer(size_bytes);
    auto* const alloc = mAllocator.allocate(desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RANGE read_range = {0, 0}; // write-only
    void* data_start_void;
    alloc->GetResource()->Map(0, &read_range, &data_start_void);
    util::set_object_name(alloc->GetResource(), "respool mapped buffer");
    return acquireBuffer(alloc, resource_state::unknown, size_bytes, stride_bytes, cc::bit_cast<std::byte*>(data_start_void));
}

void phi::d3d12::ResourcePool::free(phi::handle::resource res)
{
    CC_ASSERT(res != mInjectedBackbufferResource && "the backbuffer resource must not be freed");
    if (!res.is_valid())
        return;

    // TODO: dangle check

    // This requires no synchronization, as D3D12MA internally syncs
    resource_node& freed_node = mPool.get(static_cast<unsigned>(res.index));
    freed_node.allocation->Release();

    {
        // This is a write access to the pool and must be synced
        auto lg = std::lock_guard(mMutex);
        mPool.release(static_cast<unsigned>(res.index));
    }
}

void phi::d3d12::ResourcePool::free(cc::span<const phi::handle::resource> resources)
{
    auto lg = std::lock_guard(mMutex);

    for (auto res : resources)
    {
        CC_ASSERT(res != mInjectedBackbufferResource && "the backbuffer resource must not be freed");
        if (res.is_valid())
        {
            resource_node& freed_node = mPool.get(static_cast<unsigned>(res.index));
            // This is a write access to mAllocatorDescriptors
            freed_node.allocation->Release();
            // This is a write access to the pool and must be synced
            mPool.release(static_cast<unsigned>(res.index));
        }
    }
}

phi::handle::resource phi::d3d12::ResourcePool::acquireBuffer(
    D3D12MA::Allocation* alloc, phi::resource_state initial_state, uint64_t buffer_width, unsigned buffer_stride, std::byte* buffer_map)
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
    new_node.master_state = initial_state;
    new_node.buffer.width = static_cast<unsigned>(buffer_width);
    new_node.buffer.stride = buffer_stride;
    new_node.buffer.map = buffer_map;

    return {static_cast<handle::index_t>(res)};
}

phi::handle::resource phi::d3d12::ResourcePool::acquireImage(
    D3D12MA::Allocation* alloc, phi::format pixel_format, phi::resource_state initial_state, unsigned num_mips, unsigned num_array_layers)
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
    new_node.master_state = initial_state;
    new_node.image.num_mips = num_mips;
    new_node.image.num_array_layers = num_array_layers;
    new_node.image.pixel_format = pixel_format;

    return {static_cast<handle::index_t>(res)};
}
