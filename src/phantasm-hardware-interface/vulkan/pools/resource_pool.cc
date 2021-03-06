#include "resource_pool.hh"

#include <clean-core/bit_cast.hh>
#include <clean-core/utility.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/common/format_size.hh>
#include <phantasm-hardware-interface/common/log.hh>

#include <phantasm-hardware-interface/util.hh>
#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/util.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/common/vk_format.hh>
#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-hardware-interface/vulkan/memory/VMA.hh>

namespace
{
constexpr char const* vk_get_tex_dim_literal(phi::texture_dimension tdim)
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

constexpr VmaMemoryUsage vk_heap_to_vma(phi::resource_heap heap)
{
    switch (heap)
    {
    case phi::resource_heap::gpu:
        return VMA_MEMORY_USAGE_GPU_ONLY;
    case phi::resource_heap::upload:
        return VMA_MEMORY_USAGE_CPU_TO_GPU;
    case phi::resource_heap::readback:
        return VMA_MEMORY_USAGE_GPU_TO_CPU;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(heap);
}

constexpr char const* vk_get_heap_type_literal(phi::resource_heap heap)
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
}

phi::handle::resource phi::vk::ResourcePool::createTexture(arg::texture_description const& description, char const* dbg_name)
{
    CC_CONTRACT(description.width > 0 && description.height > 0);
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = nullptr;

    image_info.imageType = util::to_native(description.dim);
    image_info.format = util::to_vk_format(description.fmt);

    image_info.extent.width = description.width;
    image_info.extent.height = description.height;
    image_info.extent.depth = description.dim == texture_dimension::t3d ? description.depth_or_array_size : 1;
    image_info.mipLevels = description.num_mips < 1 ? phi::util::get_num_mips(description.width, description.height) : description.num_mips;
    image_info.arrayLayers = description.dim == texture_dimension::t3d ? 1 : description.depth_or_array_size;

    image_info.samples = util::to_native_sample_flags(description.num_samples);
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // TRANSFER_DST/SRC: can be copied
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    if (description.usage & resource_usage_flags::allow_uav)
    {
        // STORAGE: can be used as a UAV in shaders
        image_info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (description.usage & resource_usage_flags::allow_depth_stencil)
    {
        image_info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (description.usage & resource_usage_flags::allow_render_target)
    {
        image_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if ((description.usage & resource_usage_flags::deny_shader_resource) == 0)
    {
        // SAMPLED: can be read with a sampler;
        image_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }


    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;

    // MUTABLE_FORMAT: can be viewed with a different format
    image_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

    if (description.dim == texture_dimension::t2d && description.depth_or_array_size == 6)
    {
        // t2d[6] is likely used as a cubemap
        image_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocation res_alloc;
    VkImage res_image;
    PHI_VK_VERIFY_SUCCESS(vmaCreateImage(mAllocator, &image_info, &alloc_info, &res_image, &res_alloc, nullptr));
    util::set_object_name(mDevice, res_image, "phi tex%s[%u] %s (%ux%u, %u mips)", vk_get_tex_dim_literal(description.dim),
                          description.depth_or_array_size, dbg_name ? dbg_name : "", description.width, description.height, image_info.mipLevels);
    return acquireImage(res_alloc, res_image, description, image_info.mipLevels);
}

phi::handle::resource phi::vk::ResourcePool::createBuffer(arg::buffer_description const& desc, char const* dbg_name)
{
    CC_CONTRACT(desc.size_bytes > 0);
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = desc.size_bytes;

    // right now we'll just take all usages this thing might have in API semantics
    // it might be required down the line to restrict this (as in, make it part of API)
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
                        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;

    // NOTE: we currently do not make use of allow_uav or the heap type to restrict usage flags at all
    // allow_uav might have been a poor API decision, we might need something more finegrained instead, and have the default be allowing everything
    // problem is, in d3d12 ALLOW_UNORDERED_ACCESS is exclusive with ALLOW_DEPTH_STENCIL, so defaulting right away is not possible
    // if (allow_uav || heap == resource_heap::upload) { ... }

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = vk_heap_to_vma(desc.heap);

    VmaAllocation res_alloc;
    VkBuffer res_buffer;
    PHI_VK_VERIFY_SUCCESS(vmaCreateBuffer(mAllocator, &buffer_info, &alloc_info, &res_buffer, &res_alloc, nullptr));
    util::set_object_name(mDevice, res_buffer, "pool buf %s (%uB, %uB stride, %s heap)", dbg_name ? dbg_name : "", unsigned(desc.size_bytes),
                          desc.stride_bytes, vk_get_heap_type_literal(desc.heap));
    return acquireBuffer(res_alloc, res_buffer, buffer_info.usage, desc);
}

std::byte* phi::vk::ResourcePool::mapBuffer(phi::handle::resource res, int begin, int end)
{
    CC_ASSERT(res.is_valid() && "attempted to map invalid handle");

    resource_node& node = mPool.get(unsigned(res._value));

    CC_ASSERT(node.type == resource_node::resource_type::buffer && node.heap != resource_heap::gpu && //
              "attempted to map non-buffer or buffer on GPU heap");

    void* data_start_void;
    vmaMapMemory(mAllocator, node.allocation, &data_start_void);
    // read-write access to pool, but access to resource is user-synchronized
    node.buffer.num_vma_maps++;


    // NOTE: Vulkan terminology:
    // "flush" - make CPU -> GPU writes visible
    // "invalidate" - make CPU <- GPU reads visible
    // this ONLY applies to memory that does not have VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    // however, all PC GPUs (AMD, NVidia, Intel) are always HOST_COHERENT if they are HOST_VISIBLE
    // still, to be aligned with D3D12, we:
    //      - invalidate readback buffers on map
    //      - flush upload buffers on unmap
    //
    // further reading: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html

    if (node.heap == resource_heap::readback)
    {
        CC_ASSERT(begin >= 0 && "negative invalidation begin specified");
        vmaInvalidateAllocation(mAllocator, node.allocation, unsigned(begin), end < 0 ? node.buffer.width : unsigned(end));
    }

    return reinterpret_cast<std::byte*>(data_start_void);
}

void phi::vk::ResourcePool::unmapBuffer(phi::handle::resource res, int begin, int end)
{
    CC_ASSERT(res.is_valid() && "attempted to unmap invalid handle");

    resource_node& node = mPool.get(unsigned(res._value));

    CC_ASSERT(node.type == resource_node::resource_type::buffer && node.heap != resource_heap::gpu && //
              "attempted to unmap non-buffer or buffer on GPU heap");

    vmaUnmapMemory(mAllocator, node.allocation);
    // read-write access to pool, but access to resource is user-synchronized
    node.buffer.num_vma_maps--;
    CC_ASSERT(node.buffer.num_vma_maps >= 0 && "more unmaps than maps on resource");

    // see note in ::mapBuffer above
    if (node.heap == resource_heap::upload)
    {
        vmaFlushAllocation(mAllocator, node.allocation, begin, end < 0 ? node.buffer.width : end);
    }
}

phi::handle::resource phi::vk::ResourcePool::createBufferInternal(uint64_t size_bytes, unsigned stride_bytes, resource_heap heap, VkBufferUsageFlags usage, char const* debug_name)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size_bytes;
    buffer_info.usage = usage;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = vk_heap_to_vma(heap);

    VmaAllocation res_alloc;
    VkBuffer res_buffer;
    PHI_VK_VERIFY_SUCCESS(vmaCreateBuffer(mAllocator, &buffer_info, &alloc_info, &res_buffer, &res_alloc, nullptr));
    util::set_object_name(mDevice, res_buffer, "%s", debug_name);


    arg::buffer_description bufferDesc;
    bufferDesc.heap = heap;
    bufferDesc.allow_uav = false;
    bufferDesc.size_bytes = uint32_t(size_bytes);
    bufferDesc.stride_bytes = stride_bytes;
    return acquireBuffer(res_alloc, res_buffer, buffer_info.usage, bufferDesc);
}

void phi::vk::ResourcePool::free(phi::handle::resource res)
{
    if (!res.is_valid())
        return;
    CC_ASSERT(!isBackbuffer(res) && "the backbuffer resource must not be freed");

    resource_node& freed_node = mPool.get(res._value);
    internalFree(freed_node);
    mPool.release(res._value);
}

void phi::vk::ResourcePool::free(cc::span<const phi::handle::resource> resources)
{
    for (auto res : resources)
    {
        free(res);
    }
}

void phi::vk::ResourcePool::setDebugName(phi::handle::resource res, const char* name, unsigned name_len)
{
    CC_CONTRACT(name != nullptr);
    auto const& node = internalGet(res);
    if (node.type == resource_node::resource_type::image)
    {
        util::set_object_name(mDevice, node.image.raw_image, "%*s [respool named]", name_len, name);
    }
    else
    {
        util::set_object_name(mDevice, node.buffer.raw_buffer, "%*s [respool named]", name_len, name);
    }
}

void phi::vk::ResourcePool::initialize(VkPhysicalDevice physical, VkDevice device, unsigned max_num_resources, unsigned max_num_swapchains, cc::allocator* static_alloc)
{
    mDevice = device;
    {
        VmaAllocatorCreateInfo create_info = {};
        create_info.physicalDevice = physical;
        create_info.device = device;
        PHI_VK_VERIFY_SUCCESS(vmaCreateAllocator(&create_info, &mAllocator));
    }

    mAllocatorDescriptors.initialize(device, max_num_resources, 0, 0, 0);
    mPool.initialize(max_num_resources + max_num_swapchains, static_alloc); // additional resources for swapchain backbuffers

    mParallelResourceDescriptions.reset(static_alloc, mPool.max_size());

    mNumReservedBackbuffers = max_num_swapchains;
    mInjectedBackbufferViews = cc::alloc_array<VkImageView>::filled(mNumReservedBackbuffers, nullptr, static_alloc);
    for (auto i = 0u; i < mNumReservedBackbuffers; ++i)
    {
        auto backbuffer_reserved = mPool.acquire();
        resource_node& backbuffer_node = mPool.get(backbuffer_reserved);
        backbuffer_node.type = resource_node::resource_type::image;
        backbuffer_node.master_state = resource_state::undefined;
        backbuffer_node.heap = resource_heap::gpu;
        backbuffer_node.image.raw_image = nullptr;
        backbuffer_node.image.pixel_format = format::bgra8un;
    }

    mSingleCBVLayout = mAllocatorDescriptors.createSingleCBVLayout(false);
    mSingleCBVLayoutCompute = mAllocatorDescriptors.createSingleCBVLayout(true);
}

void phi::vk::ResourcePool::destroy()
{
    for (auto i = 0u; i < mNumReservedBackbuffers; ++i)
    {
        mPool.release(mPool.unsafe_construct_handle_for_index(i));
    }

    auto num_leaks = 0;
    mPool.iterate_allocated_nodes([&](resource_node& leaked_node) {
        if (leaked_node.allocation != nullptr)
        {
            ++num_leaks;
            internalFree(leaked_node);
        }
    });

    if (num_leaks > 0)
    {
        PHI_LOG("leaked {} handle::resource object{}", num_leaks, num_leaks == 1 ? "" : "s");
    }

    mPool.destroy();
    mParallelResourceDescriptions = {};

    vmaDestroyAllocator(mAllocator);
    mAllocator = nullptr;

    vkDestroyDescriptorSetLayout(mAllocatorDescriptors.getDevice(), mSingleCBVLayout, nullptr);
    vkDestroyDescriptorSetLayout(mAllocatorDescriptors.getDevice(), mSingleCBVLayoutCompute, nullptr);

    mAllocatorDescriptors.destroy();
}

VkDeviceMemory phi::vk::ResourcePool::getRawDeviceMemory(phi::handle::resource res) const
{
    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(mAllocator, internalGet(res).allocation, &alloc_info);
    return alloc_info.deviceMemory;
}

phi::handle::resource phi::vk::ResourcePool::injectBackbufferResource(
    unsigned swapchain_index, VkImage raw_image, phi::resource_state state, VkImageView backbuffer_view, unsigned width, unsigned height, phi::resource_state& out_prev_state)
{
    auto const res_handle = mPool.unsafe_construct_handle_for_index(swapchain_index);

    mInjectedBackbufferViews[swapchain_index] = backbuffer_view;

    resource_node& backbuffer_node = mPool.get(res_handle);
    backbuffer_node.image.raw_image = raw_image;
    out_prev_state = backbuffer_node.master_state;
    backbuffer_node.master_state = state;
    backbuffer_node.master_state_dependency = util::to_pipeline_stage_dependency(state, VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM);

    // This enum value would only be returned if the state is a SRV/UAV/CBV, which is not allowed for backbuffers (in our API, not Vulkan)
    CC_ASSERT(backbuffer_node.master_state_dependency != VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM && "backbuffer in invalid resource state");

    arg::resource_description& storedDesc = mParallelResourceDescriptions[swapchain_index];
    storedDesc = arg::resource_description::texture(format::bgra8un, tg::isize2(width, height));

    return {res_handle};
}

phi::handle::resource phi::vk::ResourcePool::acquireBuffer(VmaAllocation alloc, VkBuffer buffer, VkBufferUsageFlags usage, arg::buffer_description const& desc)
{
    bool const create_cbv_desc = (desc.size_bytes < 65536) && (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    VkDescriptorSet cbv_desc_set = nullptr;
    VkDescriptorSet cbv_desc_set_compute = nullptr;

    if (create_cbv_desc)
    {
        // This is a write access to mAllocatorDescriptors
        auto lg = std::lock_guard(mMutex);
        cbv_desc_set = mAllocatorDescriptors.allocDescriptor(mSingleCBVLayout);
        cbv_desc_set_compute = mAllocatorDescriptors.allocDescriptor(mSingleCBVLayoutCompute);
    }

    // Perform the initial update to the CBV descriptor set

    // TODO: UNIFORM_BUFFER(_DYNAMIC) cannot be larger than some
    // platform-specific limit, this right here is just a hack
    // We require separate paths in the resource pool (and therefore in the entire API)
    // for "CBV" buffers, and other buffers.
    if (create_cbv_desc)
    {
        VkDescriptorBufferInfo cbv_info = {};
        cbv_info.buffer = buffer;
        cbv_info.offset = 0;
        cbv_info.range = desc.stride_bytes > 0 ? desc.stride_bytes : desc.size_bytes; // strided CBV if present (for dynamic offset steps)

        cc::capped_vector<VkWriteDescriptorSet, 2> writes;
        {
            auto& write = writes.emplace_back();
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext = nullptr;
            write.dstSet = cbv_desc_set;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            write.descriptorCount = 1; // Just one CBV
            write.pBufferInfo = &cbv_info;
            write.dstArrayElement = 0;
            write.dstBinding = spv::cbv_binding_start;

            // same thing again, for the compute desc set
            writes.push_back(write);
            writes.back().dstSet = cbv_desc_set_compute;
        }

        vkUpdateDescriptorSets(mAllocatorDescriptors.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    unsigned const res = mPool.acquire();

    resource_node& new_node = mPool.get(res);
    new_node.allocation = alloc;
    new_node.type = resource_node::resource_type::buffer;
    new_node.heap = desc.heap;
    new_node.buffer.raw_buffer = buffer;
    new_node.buffer.raw_uniform_dynamic_ds = cbv_desc_set;
    new_node.buffer.raw_uniform_dynamic_ds_compute = cbv_desc_set_compute;
    new_node.buffer.width = desc.size_bytes;
    new_node.buffer.stride = desc.stride_bytes;
    new_node.buffer.num_vma_maps = 0;

    new_node.master_state = resource_state::undefined;
    new_node.master_state_dependency = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    uint32_t descriptionIndex = mPool.get_handle_index(res);
    arg::resource_description& storedDesc = mParallelResourceDescriptions[descriptionIndex];
    storedDesc.type = arg::resource_description::e_resource_buffer;
    storedDesc.info_buffer = desc;

    return {res};
}
phi::handle::resource phi::vk::ResourcePool::acquireImage(VmaAllocation alloc, VkImage image, arg::texture_description const& desc, uint32_t realNumMips)
{
    unsigned const res = mPool.acquire();

    resource_node& new_node = mPool.get(res);
    new_node.allocation = alloc;
    new_node.type = resource_node::resource_type::image;
    new_node.heap = resource_heap::gpu;
    new_node.image.raw_image = image;
    new_node.image.pixel_format = desc.fmt;

    new_node.master_state = resource_state::undefined;
    new_node.master_state_dependency = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    uint32_t descriptionIndex = mPool.get_handle_index(res);
    arg::resource_description& storedDesc = mParallelResourceDescriptions[descriptionIndex];
    storedDesc.type = arg::resource_description::e_resource_texture;
    storedDesc.info_texture = desc;
    storedDesc.info_texture.num_mips = realNumMips;

    return {res};
}

void phi::vk::ResourcePool::internalFree(resource_node& node)
{
    // This requires no synchronization, as VMA internally syncs
    if (node.type == resource_node::resource_type::image)
    {
        vmaDestroyImage(mAllocator, node.image.raw_image, node.allocation);
    }
    else
    {
        for (auto _ = 0; _ < node.buffer.num_vma_maps; ++_)
        {
            // clear remaining VMA maps
            vmaUnmapMemory(mAllocator, node.allocation);
        }

        vmaDestroyBuffer(mAllocator, node.buffer.raw_buffer, node.allocation);

        // This does require synchronization
        if (node.buffer.raw_uniform_dynamic_ds != nullptr)
        {
            auto lg = std::lock_guard(mMutex);
            mAllocatorDescriptors.free(node.buffer.raw_uniform_dynamic_ds);
            mAllocatorDescriptors.free(node.buffer.raw_uniform_dynamic_ds_compute);
        }
    }
}
