#pragma once

#include <atomic>
#include <mutex>

#include <clean-core/alloc_array.hh>
#include <clean-core/atomic_linked_pool.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/resources/descriptor_allocator.hh>

typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace phi::vk
{
/// The high-level allocator for resources
/// Synchronized
/// Exception: ::setResourceState (see master state cache)
class ResourcePool
{
public:
    // frontend-facing API

    /// create a 1D, 2D or 3D texture, or a 1D/2D array
    handle::resource createTexture(arg::texture_description const& description, char const* dbg_name);

    /// create a buffer, with an element stride if its an index or vertex buffer
    handle::resource createBuffer(arg::buffer_description const& desc, char const* dbg_name);

    std::byte* mapBuffer(handle::resource res, int begin = 0, int end = -1);

    void unmapBuffer(handle::resource res, int begin = 0, int end = -1);

    [[nodiscard]] handle::resource createBufferInternal(
        uint64_t size_bytes, unsigned stride_bytes, resource_heap heap, VkBufferUsageFlags usage, const char* debug_name = "PHI internal buffer");

    void free(handle::resource res);
    void free(cc::span<handle::resource const> resources);

    void setDebugName(handle::resource res, char const* name, unsigned name_len);

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
            VkBuffer raw_buffer = nullptr;
            /// a descriptor set containing a single UNIFORM_BUFFER_DYNAMIC descriptor,
            /// unconditionally created for all qualified buffers
            VkDescriptorSet raw_uniform_dynamic_ds = nullptr;
            VkDescriptorSet raw_uniform_dynamic_ds_compute = nullptr;

            // vertex size or index size
            uint32_t stride = 0;
            // VMA requires all maps to be followed by unmaps before destruction, so track maps/unmaps
            std::atomic_int num_vma_maps = {};
            uint64_t width = 0;

            bool is_access_in_bounds(uint64_t offset, uint64_t size) const { return offset + size <= width; }
        };

        struct image_info
        {
            VkImage raw_image;
            format pixel_format;
        };

    public:
        VmaAllocation allocation = nullptr;

        union
        {
            buffer_info buffer = {};
            image_info image;
        };

        VkPipelineStageFlags master_state_dependency = 0;
        resource_state master_state = resource_state::unknown;
        resource_type type = resource_type::buffer;
        phi::resource_heap heap = resource_heap::gpu;
    };

public:
    // internal API

    void initialize(VkPhysicalDevice physical, VkDevice device, unsigned max_num_resources, unsigned max_num_swapchains, cc::allocator* static_alloc);
    void destroy();

    //
    // Raw VkBuffer / VkImage access
    //

    [[nodiscard]] VkBuffer getRawBufferOrNull(handle::resource res) const { return res.is_valid() ? internalGet(res).buffer.raw_buffer : nullptr; }
    [[nodiscard]] VkBuffer getRawBuffer(handle::resource res) const { return internalGet(res).buffer.raw_buffer; }
    [[nodiscard]] VkBuffer getRawBuffer(buffer_address addr) const { return internalGet(addr.buffer).buffer.raw_buffer; }
    [[nodiscard]] VkImage getRawImage(handle::resource res) const { return internalGet(res).image.raw_image; }

    [[nodiscard]] VkDeviceMemory getRawDeviceMemory(handle::resource res) const;

    // Raw CBV uniform buffer dynamic descriptor set access
    [[nodiscard]] VkDescriptorSet getRawCBVDescriptorSet(handle::resource res) const { return internalGet(res).buffer.raw_uniform_dynamic_ds; }
    [[nodiscard]] VkDescriptorSet getRawCBVDescriptorSetCompute(handle::resource res) const
    {
        return internalGet(res).buffer.raw_uniform_dynamic_ds_compute;
    }

    // Additional information
    [[nodiscard]] bool isImage(handle::resource res) const { return internalGet(res).type == resource_node::resource_type::image; }

    [[nodiscard]] int getNumImageSamples(handle::resource res) const { return this->getTextureDescription(res).num_mips; }

    [[nodiscard]] resource_node::image_info const& getImageInfo(handle::resource res) const { return internalGet(res).image; }
    [[nodiscard]] resource_node::buffer_info const& getBufferInfo(handle::resource res) const { return internalGet(res).buffer; }

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

    bool isBufferAccessInBounds(handle::resource res, uint64_t offset, uint64_t size) const
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

    [[nodiscard]] resource_state getResourceState(handle::resource res) const { return internalGet(res).master_state; }
    [[nodiscard]] VkPipelineStageFlags getResourceStageDependency(handle::resource res) const { return internalGet(res).master_state_dependency; }

    void setResourceState(handle::resource res, resource_state new_state, VkPipelineStageFlags new_state_dep)
    {
        // This is a write access to the pool, however we require
        // no sync since it would not interfere with unrelated allocs and frees
        // and this call assumes exclusive access to the given resource
        auto& node = internalGet(res);
        node.master_state = new_state;
        node.master_state_dependency = new_state_dep;
    }

    //
    // Swapchain backbuffer resource injection
    // Swapchain backbuffers are exposed as handle::resource, so they can be interchangably
    // used with any other render target, and follow the same transition semantics
    // these handles have a limited lifetime: valid from phi::acquireBackbuffer until
    // the first of either phi::present or phi::resize
    //

    [[nodiscard]] handle::resource injectBackbufferResource(
        unsigned swapchain_index, VkImage raw_image, resource_state state, VkImageView backbuffer_view, unsigned width, unsigned height, resource_state& out_prev_state);

    [[nodiscard]] bool isBackbuffer(handle::resource res) const { return mPool.get_handle_index(res._value) < mNumReservedBackbuffers; }

    [[nodiscard]] VkImageView getBackbufferView(handle::resource res) const { return mInjectedBackbufferViews[mPool.get_handle_index(res._value)]; }

private:
    [[nodiscard]] handle::resource acquireBuffer(VmaAllocation alloc, VkBuffer buffer, VkBufferUsageFlags usage, arg::buffer_description const& desc);

    [[nodiscard]] handle::resource acquireImage(VmaAllocation alloc, VkImage buffer, arg::texture_description const& desc, uint32_t realNumMips);

    [[nodiscard]] resource_node const& internalGet(handle::resource res) const { return mPool.get(res._value); }
    [[nodiscard]] resource_node& internalGet(handle::resource res) { return mPool.get(res._value); }

    void internalFree(resource_node& node);

private:
    /// The main pool data
    cc::atomic_linked_pool<resource_node> mPool;

    /// Amount of handles (from the start) reserved for backbuffer injection
    unsigned mNumReservedBackbuffers;
    /// The image view of the currently injected backbuffer, stored separately to
    /// not take up space in resource_node, there is always just a single injected backbuffer
    cc::alloc_array<VkImageView> mInjectedBackbufferViews;

    /// Descriptor set layouts for buffer dynamic UBO descriptor sets
    /// permanently kept alive (a: no recreation required, b: drivers can crash
    /// without "data-compatible" descriptor sets being alive when binding
    /// a descriptor to compute pipelines)
    VkDescriptorSetLayout mSingleCBVLayout = nullptr;
    VkDescriptorSetLayout mSingleCBVLayoutCompute = nullptr;

    // resource descriptions for resources in the pool
    // not used internally but required for public API
    cc::alloc_array<arg::resource_description> mParallelResourceDescriptions;

    /// "Backing" allocators
    VkDevice mDevice = nullptr;
    VmaAllocator mAllocator = nullptr;
    DescriptorAllocator mAllocatorDescriptors;
    std::mutex mMutex;
};

} // namespace phi::vk
