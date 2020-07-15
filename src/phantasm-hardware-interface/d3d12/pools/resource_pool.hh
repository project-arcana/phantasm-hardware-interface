#pragma once

#include <mutex>

#include <phantasm-hardware-interface/detail/linked_pool.hh>
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

    /// create a 1D, 2D or 3D texture, or a 1D/2D array
    [[nodiscard]] handle::resource createTexture(
        format format, unsigned w, unsigned h, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav, char const* dbg_name);

    /// create a render- or depth-stencil target
    [[nodiscard]] handle::resource createRenderTarget(
        phi::format format, unsigned w, unsigned h, unsigned samples, unsigned array_size, rt_clear_value const* optimized_clear_val, char const* dbg_name);

    /// create a buffer, with an element stride if its an index or vertex buffer
    [[nodiscard]] handle::resource createBuffer(uint64_t size_bytes, unsigned stride_bytes, resource_heap heap, bool allow_uav, char const* dbg_name);

    [[nodiscard]] std::byte* mapBuffer(handle::resource res);

    void unmapBuffer(handle::resource res);

    [[nodiscard]] handle::resource createBufferInternal(uint64_t size_bytes, unsigned stride_bytes, bool allow_uav, D3D12_RESOURCE_STATES initial_state);

    void free(handle::resource res);
    void free(cc::span<handle::resource const> resources);

    void setDebugName(handle::resource res, char const* name);

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
            uint32_t width;
            uint32_t stride; ///< vertex size or index size
        };

        struct image_info
        {
            format pixel_format;
            unsigned num_mips;
            unsigned num_array_layers;
        };

    public:
        D3D12MA::Allocation* allocation = nullptr;
        ID3D12Resource* resource = nullptr;

        union {
            buffer_info buffer;
            image_info image;
        };

        D3D12_RESOURCE_STATES master_state = D3D12_RESOURCE_STATE_COMMON;
        resource_type type;
        phi::resource_heap heap;
    };

public:
    // internal API

    void initialize(ID3D12Device& device, unsigned max_num_resources, unsigned max_num_swapchains);
    void destroy();

    //
    // Raw ID3D12Resource access
    //

    [[nodiscard]] ID3D12Resource* getRawResource(handle::resource res) const { return internalGet(res).resource; }

    // Additional information
    [[nodiscard]] bool isImage(handle::resource res) const { return internalGet(res).type == resource_node::resource_type::image; }
    [[nodiscard]] resource_node::image_info const& getImageInfo(handle::resource res) const { return internalGet(res).image; }
    [[nodiscard]] resource_node::buffer_info const& getBufferInfo(handle::resource res) const { return internalGet(res).buffer; }

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
        return {data.resource->GetGPUVirtualAddress(), data.buffer.width, data.buffer.stride};
    }

    [[nodiscard]] D3D12_INDEX_BUFFER_VIEW getIndexBufferView(handle::resource res) const
    {
        auto const& data = internalGet(res);
        CC_ASSERT(data.type == resource_node::resource_type::buffer);
        return {data.resource->GetGPUVirtualAddress(), data.buffer.width, (data.buffer.stride == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT};
    }

    [[nodiscard]] D3D12_CONSTANT_BUFFER_VIEW_DESC getConstantBufferView(handle::resource res) const
    {
        auto const& data = internalGet(res);
        CC_ASSERT(data.type == resource_node::resource_type::buffer);
        return {data.resource->GetGPUVirtualAddress(), data.buffer.width};
    }

    //
    // Swapchain backbuffer resource injection
    // Swapchain backbuffers are exposed as handle::resource, so they can be interchangably
    // used with any other render target, and follow the same transition semantics
    // these handles have a limited lifetime: valid from phi::acquireBackbuffer until
    // the first of either phi::present or phi::resize
    //

    [[nodiscard]] handle::resource injectBackbufferResource(unsigned swapchain_index, ID3D12Resource* raw_resource, D3D12_RESOURCE_STATES state);

    [[nodiscard]] bool isBackbuffer(handle::resource res) const { return mPool.get_handle_index(res._value) < mNumReservedBackbuffers; }

private:
    [[nodiscard]] handle::resource acquireBuffer(D3D12MA::Allocation* alloc, D3D12_RESOURCE_STATES initial_state, uint64_t buffer_width, unsigned buffer_stride, resource_heap heap);

    [[nodiscard]] handle::resource acquireImage(D3D12MA::Allocation* alloc, format pixel_format, D3D12_RESOURCE_STATES initial_state, unsigned num_mips, unsigned num_array_layers);

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
    /// The main pool data
    phi::detail::linked_pool<resource_node> mPool;
    /// Amount of handles (from the start) reserved for backbuffer injection
    unsigned mNumReservedBackbuffers;

    /// "Backing" allocator
    ResourceAllocator mAllocator;
    std::mutex mMutex;
};

}
