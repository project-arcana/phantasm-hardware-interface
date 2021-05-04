#include "resource_pool.hh"

#include <clean-core/bit_cast.hh>
#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/format_size.hh>
#include <phantasm-hardware-interface/common/log.hh>

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

void phi::d3d12::ResourcePool::initialize(ID3D12Device* device, uint32_t max_num_resources, uint32_t max_num_swapchains, cc::allocator* static_alloc, cc::allocator* dynamic_alloc)
{
    mAllocator.initialize(device, dynamic_alloc);
    mPool.initialize(max_num_resources + max_num_swapchains, static_alloc); // additional resources for swapchain backbuffers

    mParallelResourceDescriptions.reset(static_alloc, mPool.max_size());

    mNumReservedBackbuffers = max_num_swapchains;
    for (auto i = 0u; i < mNumReservedBackbuffers; ++i)
    {
        auto backbuffer_reserved = mPool.acquire();
        (void)backbuffer_reserved;
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

    mPool.destroy();
    mParallelResourceDescriptions = {};

    mAllocator.destroy();
}

phi::handle::resource phi::d3d12::ResourcePool::injectBackbufferResource(unsigned swapchain_index, tg::isize2 size, ID3D12Resource* raw_resource, D3D12_RESOURCE_STATES state)
{
    CC_ASSERT(swapchain_index < mNumReservedBackbuffers && "swapchain index OOB");
    auto const res_handle = mPool.unsafe_construct_handle_for_index(swapchain_index);
    resource_node& backbuffer_node = mPool.get(res_handle);
    backbuffer_node.type = resource_node::resource_type::image;
    backbuffer_node.resource = raw_resource;
    backbuffer_node.master_state = state;


    arg::resource_description& storedDesc = mParallelResourceDescriptions[swapchain_index];
    storedDesc.type = arg::resource_description::e_resource_texture;
    storedDesc.texture(format::bgra8un, size);

    return {res_handle};
}

phi::handle::resource phi::d3d12::ResourcePool::createTexture(arg::texture_description const& description, char const* dbg_name)
{
    CC_CONTRACT(description.width > 0 && description.height > 0);

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = util::to_native(description.dim);
    desc.Format = util::to_dxgi_format(description.fmt);
    desc.Width = description.width;
    desc.Height = description.height;
    desc.DepthOrArraySize = UINT16(description.depth_or_array_size);
    desc.MipLevels = UINT16(description.num_mips);
    desc.SampleDesc.Count = description.num_samples;
    desc.SampleDesc.Quality = desc.SampleDesc.Count != 1 ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0;

    desc.Flags = util::to_native_resource_usage_flags(description.usage);

    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Alignment = 0;

    D3D12_RESOURCE_STATES initial_state;
    D3D12_CLEAR_VALUE* clear_value_ptr = nullptr;
    D3D12_CLEAR_VALUE clear_value;
    clear_value.Format = desc.Format;

    auto const unpacked_clearval = ::phi::util::unpack_rgba8(description.optimized_clear_value);

    if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
    {
        initial_state = util::to_native(resource_state::depth_write);

        if (description.usage & phi::resource_usage_flags::use_optimized_clear_value)
        {
            clear_value.DepthStencil.Depth = float(unpacked_clearval.r) / 255.f;
            clear_value.DepthStencil.Stencil = unpacked_clearval.g;
            clear_value_ptr = &clear_value;
        }
    }
    else if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
    {
        initial_state = util::to_native(resource_state::render_target);

        if (description.usage & phi::resource_usage_flags::use_optimized_clear_value)
        {
            clear_value.Color[0] = float(unpacked_clearval.r) / 255.f;
            clear_value.Color[1] = float(unpacked_clearval.g) / 255.f;
            clear_value.Color[2] = float(unpacked_clearval.b) / 255.f;
            clear_value.Color[3] = float(unpacked_clearval.a) / 255.f;
            clear_value_ptr = &clear_value;
        }
    }
    else
    {
        initial_state = util::to_native(resource_state::copy_dest);
    }

    auto* const alloc = mAllocator.allocate(desc, initial_state, clear_value_ptr, D3D12_HEAP_TYPE_DEFAULT);
    auto const realNumMipmaps = alloc->GetResource()->GetDesc().MipLevels;
    util::set_object_name(alloc->GetResource(), "tex%s[%u] %s (%ux%u, %u mips)", d3d12_get_tex_dim_literal(description.dim),
                          description.depth_or_array_size, dbg_name ? dbg_name : "", description.width, description.height, realNumMipmaps);

    return acquireImage(alloc, initial_state, description, realNumMipmaps);
}

phi::handle::resource phi::d3d12::ResourcePool::createBuffer(arg::buffer_description const& description, const char* dbg_name)
{
    CC_CONTRACT(description.size_bytes > 0);
    D3D12_RESOURCE_STATES const initial_state = d3d12_get_initial_state_by_heap(description.heap);

    auto desc = CD3DX12_RESOURCE_DESC::Buffer(description.size_bytes);

    if (description.allow_uav)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto* const alloc = mAllocator.allocate(desc, initial_state, nullptr, util::to_native(description.heap));
    util::set_object_name(alloc->GetResource(), "buf %s (%uB, %uB stride, %s heap)", dbg_name ? dbg_name : "", uint32_t(description.size_bytes),
                          description.stride_bytes, d3d12_get_heap_type_literal(description.heap));

    auto const res = acquireBuffer(alloc, initial_state, description);
    return res;
}

std::byte* phi::d3d12::ResourcePool::mapBuffer(phi::handle::resource res, int begin, int end)
{
    CC_ASSERT(res.is_valid() && "attempted to map invalid handle");

    resource_node const& node = mPool.get(unsigned(res._value));
    CC_ASSERT(node.type == resource_node::resource_type::buffer && node.heap != resource_heap::gpu && //
              "attempted to map non-buffer or buffer on GPU heap");


    D3D12_RANGE const range = {SIZE_T(begin), end < 0 ? node.buffer.width : SIZE_T(end)};
    void* data_start_void;
    PHI_D3D12_VERIFY(node.resource->Map(0, &range, &data_start_void));
    return reinterpret_cast<std::byte*>(data_start_void);
}

void phi::d3d12::ResourcePool::unmapBuffer(phi::handle::resource res, int begin, int end)
{
    CC_ASSERT(res.is_valid() && "attempted to unmap invalid handle");

    resource_node const& node = mPool.get(unsigned(res._value));

    CC_ASSERT(node.type == resource_node::resource_type::buffer && node.heap != resource_heap::gpu && //
              "attempted to unmap non-buffer or buffer on GPU heap");

    SIZE_T const default_width = node.heap == resource_heap::readback ? begin : node.buffer.width;
    D3D12_RANGE const range = {SIZE_T(begin), end < 0 ? default_width : SIZE_T(end)};
    node.resource->Unmap(0, &range);
}

phi::handle::resource phi::d3d12::ResourcePool::createBufferInternal(
    uint64_t size_bytes, unsigned stride_bytes, bool allow_uav, D3D12_RESOURCE_STATES initial_state, char const* debug_name)
{
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size_bytes);

    if (allow_uav)
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto* const alloc = mAllocator.allocate(desc, initial_state);
    util::set_object_name(alloc->GetResource(), "phi internal: %s", debug_name);

    arg::buffer_description bufferDesc;
    bufferDesc.heap = phi::resource_heap::gpu;
    bufferDesc.allow_uav = allow_uav;
    bufferDesc.size_bytes = uint32_t(size_bytes);
    bufferDesc.stride_bytes = stride_bytes;
    return acquireBuffer(alloc, initial_state, bufferDesc);
}

void phi::d3d12::ResourcePool::free(phi::handle::resource res)
{
    if (!res.is_valid())
        return;
    CC_ASSERT(!isBackbuffer(res) && "the backbuffer resource must not be freed");

    resource_node& freed_node = mPool.get(res._value);
    // This requires no synchronization, as D3D12MA internally syncs
    freed_node.allocation->Release();

    mPool.release(res._value);
}

void phi::d3d12::ResourcePool::free(cc::span<const phi::handle::resource> resources)
{
    for (auto res : resources)
    {
        free(res);
    }
}

void phi::d3d12::ResourcePool::setDebugName(phi::handle::resource res, const char* name, unsigned name_length)
{
    CC_CONTRACT(name != nullptr);
    util::set_object_name(internalGet(res).resource, "%*s [respool named]", name_length, name);
}

phi::handle::resource phi::d3d12::ResourcePool::acquireBuffer(D3D12MA::Allocation* alloc, D3D12_RESOURCE_STATES initial_state, arg::buffer_description const& desc)
{
    uint32_t const res = mPool.acquire();

    resource_node& new_node = mPool.get(res);
    new_node.allocation = alloc;
    new_node.resource = alloc->GetResource();
    new_node.type = resource_node::resource_type::buffer;
    new_node.heap = desc.heap;
    new_node.master_state = initial_state;
    new_node.buffer.gpu_va = new_node.resource->GetGPUVirtualAddress();
    new_node.buffer.width = desc.size_bytes;
    new_node.buffer.stride = desc.stride_bytes;

    uint32_t descriptionIndex = mPool.get_handle_index(res);
    arg::resource_description& storedDesc = mParallelResourceDescriptions[descriptionIndex];
    storedDesc.type = arg::resource_description::e_resource_buffer;
    storedDesc.info_buffer = desc;

    return {res};
}

phi::handle::resource phi::d3d12::ResourcePool::acquireImage(D3D12MA::Allocation* alloc, D3D12_RESOURCE_STATES initial_state, arg::texture_description const& desc, UINT16 realNumMipmaps)
{
    unsigned const res = mPool.acquire();

    resource_node& new_node = mPool.get(res);
    new_node.allocation = alloc;
    new_node.resource = alloc->GetResource();
    new_node.type = resource_node::resource_type::image;
    new_node.heap = resource_heap::gpu;
    new_node.master_state = initial_state;
    new_node.image.num_mips = desc.num_mips;
    new_node.image.pixel_format = desc.fmt;

    uint32_t descriptionIndex = mPool.get_handle_index(res);
    arg::resource_description& storedDesc = mParallelResourceDescriptions[descriptionIndex];
    storedDesc.type = arg::resource_description::e_resource_texture;
    storedDesc.info_texture = desc;
    storedDesc.info_texture.num_mips = uint32_t(realNumMipmaps);

    return {res};
}
