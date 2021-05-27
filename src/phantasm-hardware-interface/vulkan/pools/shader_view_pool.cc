#include "shader_view_pool.hh"

#include <clean-core/alloc_vector.hh>
#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/common/log.hh>

#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/vk_format.hh>
#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-hardware-interface/vulkan/resources/transition_barrier.hh>

#include "accel_struct_pool.hh"
#include "resource_pool.hh"

phi::handle::shader_view phi::vk::ShaderViewPool::create(cc::span<resource_view const> srvs,
                                                         cc::span<resource_view const> uavs,
                                                         cc::span<const sampler_config> sampler_configs,
                                                         bool usage_compute,
                                                         cc::allocator* scratch)
{
    // Create the layout, maps as follows:
    // SRV:
    //      Texture* -> VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
    //      RT AS    -> VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV
    //      Buffer   -> VK_DESCRIPTOR_TYPE_STORAGE_BUFFER (or UNIFORM_BUFFER, STORAGE_TEXEL_BUFFER? This one is ambiguous)
    // UAV:
    //      Texture* -> VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    //      Buffer   -> VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    auto const layout = mAllocator.createLayoutFromShaderViewArgs(srvs, uavs, uint32_t(sampler_configs.size()), usage_compute);

    auto const newSV
        = createShaderViewFromLayout(layout, uint32_t(srvs.size()), uint32_t(uavs.size()), uint32_t(sampler_configs.size()), cc::system_allocator, nullptr);

    if (srvs.size() > 0)
    {
        writeShaderViewSRVs(newSV, 0, srvs, scratch);
    }

    if (uavs.size() > 0)
    {
        writeShaderViewUAVs(newSV, 0, uavs, scratch);
    }

    if (sampler_configs.size() > 0)
    {
        writeShaderViewSamplers(newSV, 0, sampler_configs, scratch);
    }

    return newSV;
}

phi::handle::shader_view phi::vk::ShaderViewPool::createEmpty(arg::shader_view_description const& desc, bool usageCompute)
{
    auto const layout = mAllocator.createLayoutFromDescription(desc, usageCompute);

    return createShaderViewFromLayout(layout, desc.num_srvs, desc.num_uavs, desc.num_samplers, cc::system_allocator, &desc);
}

void phi::vk::ShaderViewPool::writeShaderViewSRVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> srvs, cc::allocator* scratch)
{
    auto& node = internalGet(sv);
    CC_ASSERT(srvs.size() + offset <= node.numSRVs && "SRV write out of bounds");

    cc::alloc_vector<VkWriteDescriptorSet> writes;
    writes.reset_reserve(scratch, srvs.size());

    auto F_AddWrite = [&](VkDescriptorType type, uint32_t flatIndex) {
        auto& write = writes.emplace_back_stable();
        write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext = nullptr;
        write.dstSet = node.descriptorSet;
        write.descriptorType = type;
        write.descriptorCount = 1;

        bool success = flatSRVIndexToBindingAndArrayIndex(node, flatIndex, write.dstBinding, write.dstArrayElement);
        CC_ASSERT(success && "SRV write out of bounds");
    };

    for (auto i = 0u; i < srvs.size(); ++i)
    {
        auto const& srv = srvs[i];
        auto const nativeSRVType = util::to_native_srv_desc_type(srv.dimension);
        auto const flatIdx = offset + i;

        if (srv.dimension == resource_view_dimension::buffer)
        {
            VkDescriptorBufferInfo* buf_info = scratch->new_t<VkDescriptorBufferInfo>();
            buf_info->buffer = mResourcePool->getRawBuffer(srv.resource);
            buf_info->offset = srv.buffer_info.element_start;
            buf_info->range = srv.buffer_info.num_elements * srv.buffer_info.element_stride_bytes;

            F_AddWrite(nativeSRVType, flatIdx);
            writes.back().pBufferInfo = buf_info;
            // scratch allocations can be leaked safely
        }
        else if (srv.dimension == resource_view_dimension::raytracing_accel_struct)
        {
            VkWriteDescriptorSetAccelerationStructureNV* as_info = scratch->new_t<VkWriteDescriptorSetAccelerationStructureNV>();
            *as_info = {};
            as_info->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
            as_info->accelerationStructureCount = 1;
            as_info->pAccelerationStructures = &mAccelStructPool->getNode(srv.accel_struct_info.accel_struct).raw_as;

            F_AddWrite(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, flatIdx);
            writes.back().pNext = as_info;
        }
        else // shader_view_dimension::textureX
        {
            VkImageView newImageView = makeImageView(srv, false, true);

            VkDescriptorImageInfo* img_info = scratch->new_t<VkDescriptorImageInfo>();
            img_info->imageView = newImageView;
            img_info->imageLayout = util::to_image_layout(resource_state::shader_resource);
            img_info->sampler = nullptr;

            F_AddWrite(nativeSRVType, flatIdx);
            writes.back().pImageInfo = img_info;

            // free and replace the previous image view at this slot
            uint32_t linearImageViewIndex = offset + i;
            VkImageView prevImageView = node.imageViews[linearImageViewIndex];
            if (prevImageView)
            {
                vkDestroyImageView(mAllocator.getDevice(), prevImageView, nullptr);
            }
            node.imageViews[linearImageViewIndex] = newImageView;
        }
    }

    vkUpdateDescriptorSets(mAllocator.getDevice(), uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void phi::vk::ShaderViewPool::writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs, cc::allocator* scratch)
{
    auto& node = internalGet(sv);
    // image_view.size(): total amount of UAVs + SRVs in this shader view
    CC_ASSERT(node.numSRVs + uavs.size() + offset <= node.imageViews.size() && "UAV write out of bounds");

    cc::alloc_vector<VkWriteDescriptorSet> writes;
    writes.reset_reserve(scratch, uavs.size());

    auto F_AddWrite = [&](VkDescriptorType type, uint32_t flatIndex) {
        auto& write = writes.emplace_back_stable();
        write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext = nullptr;
        write.dstSet = node.descriptorSet;
        write.descriptorType = type;
        write.descriptorCount = 1;

        bool success = flatUAVIndexToBindingAndArrayIndex(node, flatIndex, write.dstBinding, write.dstArrayElement);
        CC_ASSERT(success && "UAV write out of bounds");
    };

    for (auto i = 0u; i < uavs.size(); ++i)
    {
        auto const& uav = uavs[i];

        auto const nativeUAVType = util::to_native_uav_desc_type(uav.dimension);
        auto const flatIdx = offset + i;

        if (uav.dimension == resource_view_dimension::buffer)
        {
            VkDescriptorBufferInfo* buf_info = scratch->new_t<VkDescriptorBufferInfo>();
            buf_info->buffer = mResourcePool->getRawBuffer(uav.resource);
            buf_info->offset = uav.buffer_info.element_start;
            buf_info->range = uav.buffer_info.num_elements * uav.buffer_info.element_stride_bytes;

            F_AddWrite(nativeUAVType, flatIdx);
            writes.back().pBufferInfo = buf_info;
            // scratch allocations can be leaked safely
        }
        else
        {
            // shader_view_dimension::textureX
            CC_ASSERT(uav.dimension != resource_view_dimension::raytracing_accel_struct && "Raytracing acceleration structures not allowed as UAVs");

            VkImageView newImageView = makeImageView(uav, true, true);

            VkDescriptorImageInfo* img_info = scratch->new_t<VkDescriptorImageInfo>();
            img_info->imageView = newImageView;
            img_info->imageLayout = util::to_image_layout(resource_state::unordered_access);
            img_info->sampler = nullptr;

            F_AddWrite(nativeUAVType, flatIdx);
            writes.back().pImageInfo = img_info;

            // free and replace the previous image view at this slot
            uint32_t linearImageViewIndex = node.numSRVs + offset + i;
            VkImageView prevImageView = node.imageViews[linearImageViewIndex];
            if (prevImageView)
            {
                vkDestroyImageView(mAllocator.getDevice(), prevImageView, nullptr);
            }
            node.imageViews[linearImageViewIndex] = newImageView;
        }
    }

    vkUpdateDescriptorSets(mAllocator.getDevice(), uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void phi::vk::ShaderViewPool::writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers, cc::allocator* scratch)
{
    auto& node = internalGet(sv);
    CC_ASSERT(samplers.size() + offset <= node.samplers.size() && "Sampler write out of bounds");

    cc::alloc_vector<VkWriteDescriptorSet> writes;
    writes.reset_reserve(scratch, samplers.size());

    auto F_AddWrite = [&](VkDescriptorType type, uint32_t dest_binding) {
        auto& write = writes.emplace_back_stable();
        write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext = nullptr;
        write.dstSet = node.descriptorSet;
        write.descriptorType = type;
        write.dstArrayElement = 0;
        write.dstBinding = dest_binding;
        write.descriptorCount = 1;
    };

    for (auto i = 0u; i < samplers.size(); ++i)
    {
        auto const binding = spv::sampler_binding_start + offset + i;

        VkSampler newSampler = makeSampler(samplers[i]);

        VkDescriptorImageInfo* img_info = scratch->new_t<VkDescriptorImageInfo>();
        img_info->imageView = nullptr;
        img_info->imageLayout = util::to_image_layout(resource_state::shader_resource);
        img_info->sampler = newSampler;

        F_AddWrite(VK_DESCRIPTOR_TYPE_SAMPLER, binding);
        writes.back().pImageInfo = img_info;

        // free and replace the previous image view at this slot
        uint32_t linearSamplerIndex = offset + i;
        VkSampler prevSampler = node.samplers[linearSamplerIndex];
        if (prevSampler)
        {
            vkDestroySampler(mAllocator.getDevice(), prevSampler, nullptr);
        }
        node.samplers[linearSamplerIndex] = newSampler;
    }

    vkUpdateDescriptorSets(mAllocator.getDevice(), uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void phi::vk::ShaderViewPool::free(phi::handle::shader_view sv)
{
    if (!sv.is_valid())
        return;

    ShaderViewNode& freed_node = mPool.get(sv._value);
    internalFree(freed_node);

    {
        // This is a write access to the allocator, and must be synced
        auto lg = std::lock_guard(mMutex);
        mAllocator.free(freed_node.descriptorSet);
    }

    mPool.release(sv._value);
}

void phi::vk::ShaderViewPool::free(cc::span<const phi::handle::shader_view> svs)
{
    for (auto sv : svs)
    {
        free(sv);
    }
}

void phi::vk::ShaderViewPool::initialize(
    VkDevice device, ResourcePool* res_pool, AccelStructPool* as_pool, unsigned num_cbvs, unsigned num_srvs, unsigned num_uavs, unsigned num_samplers, cc::allocator* static_alloc)
{
    CC_ASSERT(mDevice == nullptr && "double init");
    mDevice = device;
    mResourcePool = res_pool;
    mAccelStructPool = as_pool;

    mAllocator.initialize(mDevice, num_cbvs, num_srvs, num_uavs, num_samplers);
    // Due to the fact that each shader argument represents up to one CBV, this is the upper limit for the amount of shader_view handles
    mPool.initialize(num_cbvs, static_alloc);
}

void phi::vk::ShaderViewPool::destroy()
{
    auto num_leaks = 0;
    mPool.iterate_allocated_nodes([&](ShaderViewNode& leaked_node) {
        ++num_leaks;

        internalFree(leaked_node);
        mAllocator.free(leaked_node.descriptorSet);
    });

    if (num_leaks > 0)
    {
        PHI_LOG("leaked {} handle::shader_view object{}", num_leaks, num_leaks == 1 ? "" : "s");
    }

    mAllocator.destroy();
}

VkImageView phi::vk::ShaderViewPool::makeImageView(const resource_view& sve, bool is_uav, bool restrict_usage_for_shader) const
{
    VkImageViewUsageCreateInfo usage_info;
    VkImageViewCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

    info.image = mResourcePool->getRawImage(sve.resource);
    info.viewType = util::to_native_image_view_type(sve.dimension);
    info.format = util::to_vk_format(sve.texture_info.pixel_format);

    info.subresourceRange.aspectMask = util::to_native_image_aspect(sve.texture_info.pixel_format);
    info.subresourceRange.baseMipLevel = sve.texture_info.mip_start;
    info.subresourceRange.levelCount = sve.texture_info.mip_size;
    info.subresourceRange.baseArrayLayer = sve.texture_info.array_start;
    info.subresourceRange.layerCount = sve.texture_info.array_size;

    if (info.viewType == VK_IMAGE_VIEW_TYPE_CUBE)
    {
        info.subresourceRange.layerCount = 6; // cubes always require 6 layers
        if (is_uav)
        {
            // UAVs explicitly represent cubes as 2D arrays of size 6
            info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        }
    }

    if (restrict_usage_for_shader)
    {
        // by default, an image view inherits the usage flags of the image
        // this means (for example) viewing an image with the STORAGE_BIT as sRGB gives an error,
        // because that format doesn't support that usage - even if the view is never used for storage (still works though)
        // this pNext chain struct allows restricting the usage
        info.pNext = &usage_info;

        usage_info = {};
        usage_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        usage_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

        if (is_uav)
        {
            usage_info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
    }

    VkImageView res;
    PHI_VK_VERIFY_SUCCESS(vkCreateImageView(mDevice, &info, nullptr, &res));
    return res;
}

phi::handle::shader_view phi::vk::ShaderViewPool::createShaderViewFromLayout(VkDescriptorSetLayout layout,
                                                                             uint32_t numSRVs,
                                                                             uint32_t numUAVs,
                                                                             uint32_t numSamplers,
                                                                             cc::allocator* dynamicAlloc,
                                                                             phi::arg::shader_view_description const* optDescription)
{
    // Do acquires requiring synchronization
    VkDescriptorSet res_raw;
    {
        auto lg = std::lock_guard(mMutex);
        res_raw = mAllocator.allocDescriptor(layout);
    }

    uint32_t const pool_index = mPool.acquire();

    // Populate new node
    ShaderViewNode& new_node = mPool.get(pool_index);
    new_node.descriptorSet = res_raw;
    new_node.descriptorSetLayout = layout;
    new_node.numSRVs = numSRVs;
    new_node.imageViews.reset(dynamicAlloc, numSRVs + numUAVs);
    new_node.samplers.reset(dynamicAlloc, numSamplers);
    std::memset(new_node.imageViews.data(), 0, new_node.imageViews.size_bytes());
    std::memset(new_node.samplers.data(), 0, new_node.samplers.size_bytes());

    if (optDescription)
    {
        // copy the descriptor entries from the description to this node
        auto const numEntriesSRV = optDescription->srv_entries.size();
        auto const numEntriesUAV = optDescription->uav_entries.size();
        if (numEntriesSRV + numEntriesUAV > 0)
        {
            new_node.optionalDescriptorEntries.reset(dynamicAlloc, numEntriesSRV + numEntriesUAV);
            new_node.numDescriptorEntriesSRV = numEntriesSRV;

            for (auto i = 0u; i < numEntriesSRV; ++i)
            {
                new_node.optionalDescriptorEntries[i] = optDescription->srv_entries[i];
            }

            for (auto i = 0u; i < numEntriesUAV; ++i)
            {
                new_node.optionalDescriptorEntries[numEntriesSRV + i] = optDescription->uav_entries[i];
            }
        }
    }

    return {pool_index};
}

VkSampler phi::vk::ShaderViewPool::makeSampler(const phi::sampler_config& config) const
{
    VkSamplerCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.minFilter = util::to_min_filter(config.filter);
    info.magFilter = util::to_mag_filter(config.filter);
    info.mipmapMode = util::to_mipmap_filter(config.filter);
    info.addressModeU = util::to_native(config.address_u);
    info.addressModeV = util::to_native(config.address_v);
    info.addressModeW = util::to_native(config.address_w);
    info.minLod = config.min_lod;
    info.maxLod = config.max_lod;
    info.mipLodBias = config.lod_bias;
    info.anisotropyEnable = config.filter == sampler_filter::anisotropic ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = float(config.max_anisotropy);
    info.borderColor = util::to_native(config.border_color);
    info.compareEnable = config.compare_func != sampler_compare_func::disabled ? VK_TRUE : VK_FALSE;
    info.compareOp = util::to_native(config.compare_func);

    VkSampler res;
    PHI_VK_VERIFY_SUCCESS(vkCreateSampler(mDevice, &info, nullptr, &res));
    return res;
}

void phi::vk::ShaderViewPool::internalFree(phi::vk::ShaderViewPool::ShaderViewNode& node) const
{
    // Destroy the contained image views
    for (auto const iv : node.imageViews)
    {
        if (iv == nullptr)
            continue;

        vkDestroyImageView(mDevice, iv, nullptr);
    }
    node.imageViews = {};

    // Destroy the contained samplers
    for (auto const s : node.samplers)
    {
        if (s == nullptr)
            continue;

        vkDestroySampler(mDevice, s, nullptr);
    }
    node.samplers = {};

    // destroy the descriptor set layout used for creation
    vkDestroyDescriptorSetLayout(mDevice, node.descriptorSetLayout, nullptr);
}

bool phi::vk::ShaderViewPool::flatSRVIndexToBindingAndArrayIndex(ShaderViewNode const& node, uint32_t flatIdx, uint32_t& outBinding, uint32_t& outArrayIndex) const
{
    if (node.optionalDescriptorEntries.empty())
    {
        // for shader views that were not created empty and with a description, no arrays are supported
        outBinding = flatIdx + spv::srv_binding_start;
        outArrayIndex = 0;
        return true;
    }


    uint32_t consumedFlatValues = 0;
    CC_ASSERT(node.numDescriptorEntriesSRV <= node.optionalDescriptorEntries.size() && "programmer error");
    for (auto i = 0u; i < node.numDescriptorEntriesSRV; ++i)
    {
        auto const& entry = node.optionalDescriptorEntries[i];

        if (consumedFlatValues + entry.array_size > flatIdx)
        {
            outBinding = i + spv::srv_binding_start;
            outArrayIndex = flatIdx - consumedFlatValues;
            return true;
        }

        consumedFlatValues += entry.array_size;
    }

    return false;
}

bool phi::vk::ShaderViewPool::flatUAVIndexToBindingAndArrayIndex(ShaderViewNode const& node, uint32_t flatIdx, uint32_t& outBinding, uint32_t& outArrayIndex) const
{
    if (node.optionalDescriptorEntries.empty())
    {
        // for shader views that were not created empty and with a description, no arrays are supported
        outBinding = flatIdx + spv::uav_binding_start;
        outArrayIndex = 0;
        return true;
    }


    uint32_t consumedFlatValues = 0;
    CC_ASSERT(node.numDescriptorEntriesSRV <= node.optionalDescriptorEntries.size() && "programmer error");
    for (auto i = node.numDescriptorEntriesSRV; i < node.optionalDescriptorEntries.size(); ++i)
    {
        auto const& entry = node.optionalDescriptorEntries[i];

        if (consumedFlatValues + entry.array_size > flatIdx)
        {
            outBinding = (i - node.numDescriptorEntriesSRV) + spv::uav_binding_start;
            outArrayIndex = flatIdx - consumedFlatValues;
            return true;
        }

        consumedFlatValues += entry.array_size;
    }

    return false;
}
