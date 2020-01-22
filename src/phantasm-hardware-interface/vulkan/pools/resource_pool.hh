#pragma once

#include <mutex>

#include <phantasm-hardware-interface/detail/linked_pool.hh>
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
    [[nodiscard]] handle::resource createTexture(format format, unsigned w, unsigned h, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav);

    /// create a render- or depth-stencil target
    [[nodiscard]] handle::resource createRenderTarget(format format, unsigned w, unsigned h, unsigned samples);

    /// create a buffer, with an element stride if its an index or vertex buffer
    [[nodiscard]] handle::resource createBuffer(uint64_t size_bytes, unsigned stride_bytes, bool allow_uav);

    [[nodiscard]] handle::resource createBufferInternal(uint64_t size_bytes, unsigned stride_bytes, VkBufferUsageFlags usage);

    /// create a mapped, UPLOAD_HEAP buffer, with an element stride if its an index or vertex buffer
    [[nodiscard]] handle::resource createMappedBuffer(unsigned size_bytes, unsigned stride_bytes = 0);

    [[nodiscard]] handle::resource createMappedBufferInternal(uint64_t size_bytes, unsigned stride_bytes, VkBufferUsageFlags usage);

    void free(handle::resource res);
    void free(cc::span<handle::resource const> resources);

    /// only valid for resources created with createMappedBuffer
    [[nodiscard]] std::byte* getMappedMemory(handle::resource res) { return mPool.get(static_cast<unsigned>(res.index)).buffer.map; }

    void flushMappedMemory(handle::resource res);

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
            VkBuffer raw_buffer;
            /// a descriptor set containing a single UNIFORM_BUFFER_DYNAMIC descriptor,
            /// unconditionally created for all qualified buffers
            VkDescriptorSet raw_uniform_dynamic_ds;
            VkDescriptorSet raw_uniform_dynamic_ds_compute;

            uint32_t stride; ///< vertex size or index size
            uint64_t width;
            std::byte* map;
        };

        struct image_info
        {
            VkImage raw_image;
            format pixel_format;
            unsigned num_mips;
            unsigned num_array_layers;
        };

    public:
        VmaAllocation allocation;

        union {
            buffer_info buffer;
            image_info image;
        };

        VkPipelineStageFlags master_state_dependency;
        resource_state master_state;
        resource_type type;
    };

public:
    // internal API

    void initialize(VkPhysicalDevice physical, VkDevice device, unsigned max_num_resources);
    void destroy();

    //
    // Raw VkBuffer / VkImage access
    //

    [[nodiscard]] VkBuffer getRawBuffer(handle::resource res) const { return internalGet(res).buffer.raw_buffer; }
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
    [[nodiscard]] resource_node::image_info const& getImageInfo(handle::resource res) const { return internalGet(res).image; }
    [[nodiscard]] resource_node::buffer_info const& getBufferInfo(handle::resource res) const { return internalGet(res).buffer; }

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

    [[nodiscard]] handle::resource injectBackbufferResource(VkImage raw_image, resource_state state, VkImageView backbuffer_view, resource_state& out_prev_state);

    [[nodiscard]] bool isBackbuffer(handle::resource res) const { return res == mInjectedBackbufferResource; }

    [[nodiscard]] VkImageView getBackbufferView() const { return mInjectedBackbufferView; }

private:
    [[nodiscard]] handle::resource acquireBuffer(
        VmaAllocation alloc, VkBuffer buffer, VkBufferUsageFlags usage, uint64_t buffer_width = 0, unsigned buffer_stride = 0, std::byte* buffer_map = nullptr);

    [[nodiscard]] handle::resource acquireImage(VmaAllocation alloc, VkImage buffer, format pixel_format, unsigned num_mips, unsigned num_array_layers);

    [[nodiscard]] resource_node const& internalGet(handle::resource res) const { return mPool.get(static_cast<unsigned>(res.index)); }
    [[nodiscard]] resource_node& internalGet(handle::resource res) { return mPool.get(static_cast<unsigned>(res.index)); }

    void internalFree(resource_node& node);

private:
    /// The main pool data
    phi::detail::linked_pool<resource_node, unsigned> mPool;

    /// The handle of the injected backbuffer resource
    handle::resource mInjectedBackbufferResource = handle::null_resource;

    /// The image view of the currently injected backbuffer, stored separately to
    /// not take up space in resource_node, there is always just a single injected backbuffer
    VkImageView mInjectedBackbufferView = nullptr;

    /// Descriptor set layouts for buffer dynamic UBO descriptor sets
    /// permanently kept alive (a: no recreation required, b: drivers can crash
    /// without "data-compatible" descriptor sets being alive when binding
    /// a descriptor to compute pipelines)
    VkDescriptorSetLayout mSingleCBVLayout = nullptr;
    VkDescriptorSetLayout mSingleCBVLayoutCompute = nullptr;

    /// "Backing" allocators
    VkDevice mDevice = nullptr;
    VmaAllocator mAllocator = nullptr;
    DescriptorAllocator mAllocatorDescriptors;
    std::mutex mMutex;
};

}
