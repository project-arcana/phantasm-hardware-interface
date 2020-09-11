#pragma once

#include <mutex>

#include <clean-core/alloc_array.hh>

#include <phantasm-hardware-interface/common/container/linked_pool.hh>
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
    [[nodiscard]] handle::resource createTexture(
        format format, unsigned w, unsigned h, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav, char const* dbg_name);

    /// create a render- or depth-stencil target
    [[nodiscard]] handle::resource createRenderTarget(format format, unsigned w, unsigned h, unsigned samples, unsigned array_size, char const* dbg_name);

    /// create a buffer, with an element stride if its an index or vertex buffer
    [[nodiscard]] handle::resource createBuffer(uint64_t size_bytes, unsigned stride_bytes, resource_heap heap, bool allow_uav, char const* dbg_name);

    std::byte* mapBuffer(handle::resource res, int begin = 0, int end = -1);

    void unmapBuffer(handle::resource res, int begin = 0, int end = -1);

    [[nodiscard]] handle::resource createBufferInternal(uint64_t size_bytes, unsigned stride_bytes, resource_heap heap, VkBufferUsageFlags usage);

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
            VkBuffer raw_buffer;
            /// a descriptor set containing a single UNIFORM_BUFFER_DYNAMIC descriptor,
            /// unconditionally created for all qualified buffers
            VkDescriptorSet raw_uniform_dynamic_ds;
            VkDescriptorSet raw_uniform_dynamic_ds_compute;

            uint32_t stride;  ///< vertex size or index size
            int num_vma_maps; ///< VMA requires all maps to be followed by unmaps before destruction, so track maps/unmaps
            uint64_t width;
        };

        struct image_info
        {
            VkImage raw_image;
            format pixel_format;
            unsigned num_mips;
            unsigned num_array_layers;
            unsigned num_samples;
            int width;
            int height;
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
        phi::resource_heap heap;
    };

public:
    // internal API

    void initialize(VkPhysicalDevice physical, VkDevice device, unsigned max_num_resources, unsigned max_num_swapchains, cc::allocator *static_alloc);
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

    [[nodiscard]] int getNumImageSamples(handle::resource res) const
    {
        auto const& node = internalGet(res);
        CC_ASSERT(node.type == resource_node::resource_type::image && "queried amount of image samples from non-image");
        return node.image.num_samples;
    }

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

    [[nodiscard]] handle::resource injectBackbufferResource(
        unsigned swapchain_index, VkImage raw_image, resource_state state, VkImageView backbuffer_view, unsigned width, unsigned height, resource_state& out_prev_state);

    [[nodiscard]] bool isBackbuffer(handle::resource res) const { return mPool.get_handle_index(res._value) < mNumReservedBackbuffers; }

    [[nodiscard]] VkImageView getBackbufferView(handle::resource res) const { return mInjectedBackbufferViews[mPool.get_handle_index(res._value)]; }

private:
    [[nodiscard]] handle::resource acquireBuffer(VmaAllocation alloc, VkBuffer buffer, VkBufferUsageFlags usage, uint64_t buffer_width, unsigned buffer_stride, resource_heap heap);

    [[nodiscard]] handle::resource acquireImage(
        VmaAllocation alloc, VkImage buffer, format pixel_format, unsigned num_mips, unsigned num_array_layers, unsigned num_samples, int width, int height);

    [[nodiscard]] resource_node const& internalGet(handle::resource res) const { return mPool.get(res._value); }
    [[nodiscard]] resource_node& internalGet(handle::resource res) { return mPool.get(res._value); }

    void internalFree(resource_node& node);

private:
    /// The main pool data
    phi::linked_pool<resource_node> mPool;

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

    /// "Backing" allocators
    VkDevice mDevice = nullptr;
    VmaAllocator mAllocator = nullptr;
    DescriptorAllocator mAllocatorDescriptors;
    std::mutex mMutex;
};

}
