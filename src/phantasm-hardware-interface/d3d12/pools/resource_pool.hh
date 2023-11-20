#pragma once

#include <clean-core/atomic_linked_pool.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/memory/ResourceAllocator.hh>

namespace phi::d3d12
{
/// The high-level allocator for resources
/// Synchronized
/// Exception: ::setResourceState (see master state cache)
class ResourcePool
{
public:
    // frontend-facing API

    handle::resource createTexture(arg::texture_description const& desc, char const* dbg_name);

    /// create a buffer, with an element stride if its an index or vertex buffer
    handle::resource createBuffer(arg::buffer_description const& desc, char const* dbg_name);

    [[nodiscard]] std::byte* mapBuffer(handle::resource res, int begin = 0, int end = -1);

    void unmapBuffer(handle::resource res, int begin = 0, int end = -1);

    [[nodiscard]] handle::resource createBufferInternal(
        uint64_t size_bytes, unsigned stride_bytes, bool allow_uav, D3D12_RESOURCE_STATES initial_state, const char* debug_name = "unnamed internal buffer");

    void free(handle::resource res);
    void free(cc::span<handle::resource const> resources);

    void setDebugName(handle::resource res, char const* name, unsigned name_length);

public:
    struct resource_node
    {
    public:
        enum class resource_type : uint8_t
        {
            buffer,
            image
        };

        struct buffer_info
        {
            D3D12_GPU_VIRTUAL_ADDRESS gpu_va; // cached
            uint32_t width;                   // for bound checks, copy ranges, VBVs
            uint32_t stride;                  // vertex/index size, structured buffer stride

            bool is_access_in_bounds(size_t offset, size_t size) const { return offset + size <= size_t(width); }
        };

        struct image_info
        {
            format pixel_format; // for byte size of image
            unsigned num_mips;   // for subresource index
        };

    public:
        D3D12MA::Allocation* allocation = nullptr;
        ID3D12Resource* resource = nullptr;

        union
        {
            buffer_info buffer;
            image_info image;
        };

        D3D12_RESOURCE_STATES master_state = D3D12_RESOURCE_STATE_COMMON;
        resource_type type;
        phi::resource_heap heap;
    };

public:
    // internal API

    void initialize(ID3D12Device* device, uint32_t max_num_resources, uint32_t max_num_swapchains, cc::allocator* static_alloc, cc::allocator* dynamic_alloc);
    void destroy();

    //
    // Raw ID3D12Resource access
    //

    ID3D12Resource* getRawResource(handle::resource res) const { return internalGet(res).resource; }
    ID3D12Resource* getRawResource(buffer_address const& addr) const { return internalGet(addr.buffer).resource; }

    ID3D12Resource* getRawResourceOrNull(handle::resource res) const { return res.is_valid() ? internalGet(res).resource : nullptr; }
    ID3D12Resource* getRawResourceOrNull(buffer_address const& addr) const
    {
        return addr.buffer.is_valid() ? internalGet(addr.buffer).resource : nullptr;
    }

    // Additional information
    bool isImage(handle::resource res) const { return internalGet(res).type == resource_node::resource_type::image; }
    bool isBuffer(handle::resource res) const { return internalGet(res).type == resource_node::resource_type::buffer; }
    resource_node::image_info const& getImageInfo(handle::resource res) const { return internalGet(res).image; }
    resource_node::buffer_info const& getBufferInfo(handle::resource res) const { return internalGet(res).buffer; }
    resource_node::buffer_info const& getBufferInfo(buffer_address const& addr) const { return internalGet(addr.buffer).buffer; }

    D3D12_GPU_VIRTUAL_ADDRESS getBufferAddrVA(buffer_address address) const
    {
        return internalGet(address.buffer).buffer.gpu_va + address.offset_bytes;
    }

    arg::resource_description const& getResourceDescription(handle::resource res) const
    {
        return mParallelResourceDescriptions[mPool.get_handle_index(res._value)];
    }

    arg::buffer_description const& getBufferDescription(handle::resource res) const
    {
        auto const& description = getResourceDescription(res);
        CC_ASSERT(description.type == arg::resource_description::e_resource_buffer && "Attempted to interpret texture as buffer");
        return description.info_buffer;
    }

    arg::texture_description const& getTextureDescription(handle::resource res) const
    {
        auto const& description = getResourceDescription(res);
        CC_ASSERT(description.type == arg::resource_description::e_resource_texture && "Attempted to interpret texture as buffer");
        return description.info_texture;
    }

    bool isBufferAccessInBounds(handle::resource res, size_t offset, size_t size) const
    {
        auto const& internal = internalGet(res);
        if (internal.type != resource_node::resource_type::buffer)
            return false;

        return internal.buffer.is_access_in_bounds(offset, size);
    }

    bool isBufferAccessInBounds(buffer_address address, size_t size) const
    {
        return isBufferAccessInBounds(address.buffer, address.offset_bytes, size);
    }

    bool isBufferAccessInBounds(buffer_range range) const { return isBufferAccessInBounds(range.buffer, range.offset_bytes, range.size_bytes); }

    //
    // Master state access
    //

    [[nodiscard]] D3D12_RESOURCE_STATES getResourceState(handle::resource res) const { return internalGet(res).master_state; }

    void setResourceState(handle::resource res, D3D12_RESOURCE_STATES new_state)
    {
        // This is a write access to the pool, however we require
        // no sync since it would not interfere with unrelated allocs and frees
        // and this call assumes exclusive access to the given resource
        internalGet(res).master_state = new_state;
    }

    //
    // CPU buffer view creation
    //

    [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW getVertexBufferView(handle::resource res) const
    {
        auto const& data = internalGet(res);
        CC_ASSERT(data.type == resource_node::resource_type::buffer);
        return {data.buffer.gpu_va, data.buffer.width, data.buffer.stride};
    }

    [[nodiscard]] D3D12_INDEX_BUFFER_VIEW getIndexBufferView(handle::resource res) const
    {
        auto const& data = internalGet(res);
        CC_ASSERT(data.type == resource_node::resource_type::buffer);

        CC_ASSERT((data.buffer.stride == 4 || data.buffer.stride == 2) && "Buffers used as index buffers must specify a stride of 4B (R32) or 2B (R16)");

        return {data.buffer.gpu_va, data.buffer.width, (data.buffer.stride == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT};
    }

    [[nodiscard]] D3D12_CONSTANT_BUFFER_VIEW_DESC getConstantBufferView(handle::resource res) const
    {
        auto const& data = internalGet(res);
        CC_ASSERT(data.type == resource_node::resource_type::buffer);
        return {data.buffer.gpu_va, data.buffer.width};
    }

    //
    // Swapchain backbuffer resource injection
    // Swapchain backbuffers are exposed as handle::resource, so they can be interchangably
    // used with any other render target, and follow the same transition semantics
    // these handles have a limited lifetime: valid from phi::acquireBackbuffer until
    // the first of either phi::present or phi::resize
    //

    [[nodiscard]] handle::resource injectBackbufferResource(unsigned swapchain_index, tg::isize2 size, format fmt, ID3D12Resource* raw_resource, D3D12_RESOURCE_STATES state);

    [[nodiscard]] bool isBackbuffer(handle::resource res) const { return mPool.get_handle_index(res._value) < mNumReservedBackbuffers; }

private:
    [[nodiscard]] handle::resource acquireBuffer(D3D12MA::Allocation* alloc, D3D12_RESOURCE_STATES initial_state, arg::buffer_description const& desc);

    [[nodiscard]] handle::resource acquireImage(D3D12MA::Allocation* alloc, D3D12_RESOURCE_STATES initial_state, arg::texture_description const& desc, UINT16 realNumMipmaps);

    [[nodiscard]] resource_node const& internalGet(handle::resource res) const
    {
        CC_ASSERT(res.is_valid() && "invalid resource handle");
        return mPool.get(res._value);
    }
    [[nodiscard]] resource_node& internalGet(handle::resource res)
    {
        CC_ASSERT(res.is_valid() && "invalid resource handle");
        return mPool.get(res._value);
    }

private:
    /// The main pool - always gen checked
    cc::atomic_linked_pool<resource_node, true> mPool;
    /// Amount of handles (from the start) reserved for backbuffer injection
    uint32_t mNumReservedBackbuffers;

    // resource descriptions for resources in the pool
    // not used internally but required for public API
    cc::alloc_array<arg::resource_description> mParallelResourceDescriptions;

    /// "Backing" allocator
    ResourceAllocator mAllocator;
};

} // namespace phi::d3d12
