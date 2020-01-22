#include "resource_pool.hh"

#include <clean-core/bit_cast.hh>
#include <clean-core/utility.hh>

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/vulkan/common/log.hh>
#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/util.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/common/vk_format.hh>
#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-hardware-interface/vulkan/memory/VMA.hh>

namespace
{
unsigned calculate_num_mip_levels(unsigned width, unsigned height)
{
    return static_cast<unsigned>(tg::floor(tg::log2(static_cast<float>(cc::max(width, height))))) + 1u;
}

constexpr char const* get_tex_dim_literal(pr::backend::texture_dimension tdim)
{
    using td = pr::backend::texture_dimension;
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

pr::backend::handle::resource pr::backend::vk::ResourcePool::createTexture(
    format format, unsigned w, unsigned h, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav)
{
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = nullptr;

    image_info.imageType = util::to_native(dim);
    image_info.format = util::to_vk_format(format);

    image_info.extent.width = uint32_t(w);
    image_info.extent.height = uint32_t(h);
    image_info.extent.depth = dim == texture_dimension::t3d ? depth_or_array_size : 1;
    image_info.mipLevels = mips < 1 ? calculate_num_mip_levels(w, h) : uint32_t(mips);
    image_info.arrayLayers = dim == texture_dimension::t3d ? 1 : depth_or_array_size;

    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    if (allow_uav)
    {
        // TODO: Image usage transfer src might deserve its own option, this is coarse
        // in fact we might want to create a pr::texture_usage enum
        image_info.usage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    image_info.flags = 0;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;

    if (dim == texture_dimension::t2d && depth_or_array_size == 6)
    {
        image_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocation res_alloc;
    VkImage res_image;
    PR_VK_VERIFY_SUCCESS(vmaCreateImage(mAllocator, &image_info, &alloc_info, &res_image, &res_alloc, nullptr));
    util::set_object_name(mDevice, res_image, "respool texture%s[%u] m%u", get_tex_dim_literal(dim), depth_or_array_size, image_info.mipLevels);
    return acquireImage(res_alloc, res_image, format, image_info.mipLevels, image_info.arrayLayers);
}

pr::backend::handle::resource pr::backend::vk::ResourcePool::createRenderTarget(pr::backend::format format, unsigned w, unsigned h, unsigned samples)
{
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = nullptr;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = util::to_vk_format(format);
    image_info.extent.width = static_cast<uint32_t>(w);
    image_info.extent.height = static_cast<uint32_t>(h);
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;

    image_info.samples = util::to_native_sample_flags(samples);
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (backend::is_depth_format(format))
        image_info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    else
        image_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    image_info.flags = 0;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocation res_alloc;
    VkImage res_image;
    PR_VK_VERIFY_SUCCESS(vmaCreateImage(mAllocator, &image_info, &alloc_info, &res_image, &res_alloc, nullptr));

    if (backend::is_depth_format(format))
        util::set_object_name(mDevice, res_image, "respool depth stencil target");
    else
        util::set_object_name(mDevice, res_image, "respool render target");

    return acquireImage(res_alloc, res_image, format, image_info.mipLevels, image_info.arrayLayers);
}

pr::backend::handle::resource pr::backend::vk::ResourcePool::createBuffer(uint64_t size_bytes, unsigned stride_bytes, bool allow_uav)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size_bytes;

    // right now we'll just take all usages this thing might have in API semantics
    // it might be required down the line to restrict this (as in, make it part of API)
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    if (allow_uav)
        buffer_info.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocation res_alloc;
    VkBuffer res_buffer;
    PR_VK_VERIFY_SUCCESS(vmaCreateBuffer(mAllocator, &buffer_info, &alloc_info, &res_buffer, &res_alloc, nullptr));
    util::set_object_name(mDevice, res_buffer, "respool buffer");
    return acquireBuffer(res_alloc, res_buffer, buffer_info.usage, size_bytes, stride_bytes);
}

pr::backend::handle::resource pr::backend::vk::ResourcePool::createBufferInternal(uint64_t size_bytes, unsigned stride_bytes, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size_bytes;
    buffer_info.usage = usage;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VmaAllocation res_alloc;
    VkBuffer res_buffer;
    PR_VK_VERIFY_SUCCESS(vmaCreateBuffer(mAllocator, &buffer_info, &alloc_info, &res_buffer, &res_alloc, nullptr));
    util::set_object_name(mDevice, res_buffer, "respool internal buffer");
    return acquireBuffer(res_alloc, res_buffer, buffer_info.usage, size_bytes, stride_bytes);
}

pr::backend::handle::resource pr::backend::vk::ResourcePool::createMappedBuffer(unsigned size_bytes, unsigned stride_bytes)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size_bytes;
    // right now we'll just take all usages this thing might have in API semantics
    // it might be required down the line to restrict this (as in, make it part of API)
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                        | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocation res_alloc;
    VmaAllocationInfo res_alloc_info;
    VkBuffer res_buffer;
    PR_VK_VERIFY_SUCCESS(vmaCreateBuffer(mAllocator, &buffer_info, &alloc_info, &res_buffer, &res_alloc, &res_alloc_info));
    CC_ASSERT(res_alloc_info.pMappedData != nullptr);
    util::set_object_name(mDevice, res_buffer, "respool mapped buffer");
    return acquireBuffer(res_alloc, res_buffer, buffer_info.usage, size_bytes, stride_bytes, cc::bit_cast<std::byte*>(res_alloc_info.pMappedData));
}

pr::backend::handle::resource pr::backend::vk::ResourcePool::createMappedBufferInternal(uint64_t size_bytes, unsigned stride_bytes, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size_bytes;
    buffer_info.usage = usage;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocation res_alloc;
    VmaAllocationInfo res_alloc_info;
    VkBuffer res_buffer;
    PR_VK_VERIFY_SUCCESS(vmaCreateBuffer(mAllocator, &buffer_info, &alloc_info, &res_buffer, &res_alloc, &res_alloc_info));
    CC_ASSERT(res_alloc_info.pMappedData != nullptr);
    util::set_object_name(mDevice, res_buffer, "respool internal mapped buffer");
    return acquireBuffer(res_alloc, res_buffer, buffer_info.usage, size_bytes, stride_bytes, cc::bit_cast<std::byte*>(res_alloc_info.pMappedData));
}

void pr::backend::vk::ResourcePool::free(pr::backend::handle::resource res)
{
    CC_ASSERT(res != mInjectedBackbufferResource && "the backbuffer resource must not be freed");
    if (!res.is_valid())
        return;

    resource_node& freed_node = mPool.get(static_cast<unsigned>(res.index));

    {
        auto lg = std::lock_guard(mMutex);
        // This is a write access to mAllocatorDescriptors
        internalFree(freed_node);
        // This is a write access to the pool and must be synced
        mPool.release(static_cast<unsigned>(res.index));
    }
}

void pr::backend::vk::ResourcePool::free(cc::span<const pr::backend::handle::resource> resources)
{
    auto lg = std::lock_guard(mMutex);

    for (auto res : resources)
    {
        CC_ASSERT(res != mInjectedBackbufferResource && "the backbuffer resource must not be freed");
        if (res.is_valid())
        {
            resource_node& freed_node = mPool.get(static_cast<unsigned>(res.index));
            // This is a write access to mAllocatorDescriptors
            internalFree(freed_node);
            // This is a write access to the pool and must be synced
            mPool.release(static_cast<unsigned>(res.index));
        }
    }
}

void pr::backend::vk::ResourcePool::flushMappedMemory(pr::backend::handle::resource res)
{
    auto const& node = internalGet(res);
    CC_ASSERT(node.type == resource_node::resource_type::buffer && node.buffer.map != nullptr && "given resource is not a mapped buffer");
    vmaFlushAllocation(mAllocator, node.allocation, 0, node.buffer.width);
}

void pr::backend::vk::ResourcePool::initialize(VkPhysicalDevice physical, VkDevice device, unsigned max_num_resources)
{
    mDevice = device;
    {
        VmaAllocatorCreateInfo create_info = {};
        create_info.physicalDevice = physical;
        create_info.device = device;
        PR_VK_VERIFY_SUCCESS(vmaCreateAllocator(&create_info, &mAllocator));
    }

    mAllocatorDescriptors.initialize(device, max_num_resources, 0, 0, 0);
    mPool.initialize(max_num_resources + 1); // 1 additional resource for the backbuffer

    {
        mInjectedBackbufferResource = {static_cast<handle::index_t>(mPool.acquire())};
        resource_node& backbuffer_node = mPool.get(static_cast<unsigned>(mInjectedBackbufferResource.index));
        backbuffer_node.type = resource_node::resource_type::image;
        backbuffer_node.master_state = resource_state::undefined;
    }

    mSingleCBVLayout = mAllocatorDescriptors.createSingleCBVLayout(false);
    mSingleCBVLayoutCompute = mAllocatorDescriptors.createSingleCBVLayout(true);
}

void pr::backend::vk::ResourcePool::destroy()
{
    auto num_leaks = 0;
    mPool.iterate_allocated_nodes([&](resource_node& leaked_node, unsigned) {
        if (leaked_node.allocation != nullptr)
        {
            ++num_leaks;
            internalFree(leaked_node);
        }
    });

    if (num_leaks > 0)
    {
        log::info()("warning: leaked {} handle::resource object{}", num_leaks, num_leaks == 1 ? "" : "s");
    }

    vmaDestroyAllocator(mAllocator);
    mAllocator = nullptr;

    vkDestroyDescriptorSetLayout(mAllocatorDescriptors.getDevice(), mSingleCBVLayout, nullptr);
    vkDestroyDescriptorSetLayout(mAllocatorDescriptors.getDevice(), mSingleCBVLayoutCompute, nullptr);

    mAllocatorDescriptors.destroy();
}

VkDeviceMemory pr::backend::vk::ResourcePool::getRawDeviceMemory(pr::backend::handle::resource res) const
{
    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(mAllocator, internalGet(res).allocation, &alloc_info);
    return alloc_info.deviceMemory;
}

pr::backend::handle::resource pr::backend::vk::ResourcePool::injectBackbufferResource(VkImage raw_image,
                                                                                      pr::backend::resource_state state,
                                                                                      VkImageView backbuffer_view,
                                                                                      pr::backend::resource_state& out_prev_state)
{
    resource_node& backbuffer_node = mPool.get(static_cast<unsigned>(mInjectedBackbufferResource.index));
    backbuffer_node.image.raw_image = raw_image;
    mInjectedBackbufferView = backbuffer_view;

    out_prev_state = backbuffer_node.master_state;
    backbuffer_node.master_state = state;
    backbuffer_node.master_state_dependency = util::to_pipeline_stage_dependency(state, VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM);

    // This enum value would only be returned if the state is a SRV/UAV/CBV, which is not allowed for backbuffers (in our API, not Vulkan)
    CC_ASSERT(backbuffer_node.master_state_dependency != VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM && "backbuffer in invalid resource state");

    return mInjectedBackbufferResource;
}

pr::backend::handle::resource pr::backend::vk::ResourcePool::acquireBuffer(
    VmaAllocation alloc, VkBuffer buffer, VkBufferUsageFlags usage, uint64_t buffer_width, unsigned buffer_stride, std::byte* buffer_map)
{
    bool const create_cbv_desc = (buffer_width < 65536) && (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    unsigned res;
    VkDescriptorSet cbv_desc_set = nullptr;
    VkDescriptorSet cbv_desc_set_compute = nullptr;
    {
        auto lg = std::lock_guard(mMutex);

        // This is a write access to the pool and must be synced
        res = mPool.acquire();

        if (create_cbv_desc)
        {
            // This is a write access to mAllocatorDescriptors
            cbv_desc_set = mAllocatorDescriptors.allocDescriptor(mSingleCBVLayout);
            cbv_desc_set_compute = mAllocatorDescriptors.allocDescriptor(mSingleCBVLayoutCompute);
        }
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
        cbv_info.range = cc::min<uint64_t>(256, buffer_width);

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

    resource_node& new_node = mPool.get(res);
    new_node.allocation = alloc;
    new_node.type = resource_node::resource_type::buffer;
    new_node.buffer.raw_buffer = buffer;
    new_node.buffer.raw_uniform_dynamic_ds = cbv_desc_set;
    new_node.buffer.raw_uniform_dynamic_ds_compute = cbv_desc_set_compute;
    new_node.buffer.width = buffer_width;
    new_node.buffer.stride = buffer_stride;
    new_node.buffer.map = buffer_map;

    new_node.master_state = resource_state::undefined;
    new_node.master_state_dependency = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    return {static_cast<handle::index_t>(res)};
}
pr::backend::handle::resource pr::backend::vk::ResourcePool::acquireImage(VmaAllocation alloc, VkImage image, format pixel_format, unsigned num_mips, unsigned num_array_layers)
{
    unsigned res;
    {
        // This is a write access to the pool and must be synced
        auto lg = std::lock_guard(mMutex);
        res = mPool.acquire();
    }
    resource_node& new_node = mPool.get(res);
    new_node.allocation = alloc;
    new_node.type = resource_node::resource_type::image;
    new_node.image.raw_image = image;
    new_node.image.pixel_format = pixel_format;
    new_node.image.num_mips = num_mips;
    new_node.image.num_array_layers = num_array_layers;

    new_node.master_state = resource_state::undefined;
    new_node.master_state_dependency = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    return {static_cast<handle::index_t>(res)};
}

void pr::backend::vk::ResourcePool::internalFree(resource_node& node)
{
    // This requires no synchronization, as VMA internally syncs
    if (node.type == resource_node::resource_type::image)
    {
        vmaDestroyImage(mAllocator, node.image.raw_image, node.allocation);
    }
    else
    {
        vmaDestroyBuffer(mAllocator, node.buffer.raw_buffer, node.allocation);

        // This does require synchronization
        if (node.buffer.raw_uniform_dynamic_ds != nullptr)
        {
            mAllocatorDescriptors.free(node.buffer.raw_uniform_dynamic_ds);
            mAllocatorDescriptors.free(node.buffer.raw_uniform_dynamic_ds_compute);
        }
    }
}
