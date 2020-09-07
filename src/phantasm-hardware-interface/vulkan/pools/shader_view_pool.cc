#include "shader_view_pool.hh"

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/detail/log.hh>

#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/vk_format.hh>
#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-hardware-interface/vulkan/resources/transition_barrier.hh>

#include "resource_pool.hh"

phi::handle::shader_view phi::vk::ShaderViewPool::create(cc::span<resource_view const> srvs,
                                                         cc::span<resource_view const> uavs,
                                                         cc::span<const sampler_config> sampler_configs,
                                                         bool usage_compute)
{
    // Create the layout, maps as follows:
    // SRV:
    //      Texture* -> VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
    //      RT AS    -> VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV
    //      Buffer   -> VK_DESCRIPTOR_TYPE_STORAGE_BUFFER (or UNIFORM_BUFFER, STORAGE_TEXEL_BUFFER? This one is ambiguous)
    // UAV:
    //      Texture* -> VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    //      Buffer   -> VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    auto const layout = mAllocator.createLayoutFromShaderViewArgs(srvs, uavs, static_cast<unsigned>(sampler_configs.size()), usage_compute);

    // Do acquires requiring synchronization
    VkDescriptorSet res_raw;
    unsigned pool_index;
    {
        auto lg = std::lock_guard(mMutex);
        res_raw = mAllocator.allocDescriptor(layout);
        pool_index = mPool.acquire();
    }

    // Populate new node
    shader_view_node& new_node = mPool.get(pool_index);
    new_node.raw_desc_set = res_raw;
    new_node.raw_desc_set_layout = layout;

    // Perform the writes
    {
        cc::capped_vector<VkWriteDescriptorSet, 16> writes;
        cc::capped_vector<VkDescriptorBufferInfo, 64> buffer_infos;
        cc::capped_vector<VkDescriptorImageInfo, 64 + limits::max_shader_samplers> image_infos;

        auto const perform_write = [&](VkDescriptorType type, unsigned dest_binding, bool is_image) {
            auto& write = writes.emplace_back();
            write = {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext = nullptr;
            write.dstSet = res_raw;
            write.descriptorType = type;
            write.dstArrayElement = 0;
            write.dstBinding = dest_binding;
            write.descriptorCount = 1;

            if (is_image)
                write.pImageInfo = image_infos.data() + (image_infos.size() - 1);
            else
                write.pBufferInfo = buffer_infos.data() + (buffer_infos.size() - 1);
        };

        for (auto i = 0u; i < uavs.size(); ++i)
        {
            auto const& uav = uavs[i];

            auto const uav_native_type = util::to_native_uav_desc_type(uav.dimension);
            auto const binding = spv::uav_binding_start + i;

            if (uav.dimension == resource_view_dimension::buffer)
            {
                auto& uav_info = buffer_infos.emplace_back();
                uav_info.buffer = mResourcePool->getRawBuffer(uav.resource);
                uav_info.offset = uav.buffer_info.element_start;
                uav_info.range = uav.buffer_info.num_elements * uav.buffer_info.element_stride_bytes;

                perform_write(uav_native_type, binding, false);
            }
            else
            {
                // shader_view_dimension::textureX
                CC_ASSERT(uav.dimension != resource_view_dimension::raytracing_accel_struct && "Raytracing acceleration structures not allowed as UAVs");

                auto& img_info = image_infos.emplace_back();
                img_info.imageView = makeImageView(uav, true);
                img_info.imageLayout = util::to_image_layout(resource_state::unordered_access);
                img_info.sampler = nullptr;

                perform_write(uav_native_type, binding, true);
            }
        }

        for (auto i = 0u; i < srvs.size(); ++i)
        {
            auto const& srv = srvs[i];

            auto const srv_native_type = util::to_native_srv_desc_type(srv.dimension);
            auto const binding = spv::srv_binding_start + i;

            if (srv.dimension == resource_view_dimension::buffer)
            {
                auto& uav_info = buffer_infos.emplace_back();
                uav_info.buffer = mResourcePool->getRawBuffer(srv.resource);
                uav_info.offset = srv.buffer_info.element_start;
                uav_info.range = srv.buffer_info.num_elements * srv.buffer_info.element_stride_bytes;

                perform_write(srv_native_type, binding, false);
            }
            else if (srv.dimension == resource_view_dimension::raytracing_accel_struct)
            {
                CC_RUNTIME_ASSERT(false && "Unimplemented!");

                VkWriteDescriptorSetAccelerationStructureNV as_info = {};
                as_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
                as_info.accelerationStructureCount = 1;
                as_info.pAccelerationStructures = nullptr; // TODO: Retrieve from res pool

                auto& write = writes.emplace_back();
                write = {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.pNext = &as_info; // TODO: keep as_info alive
                write.dstSet = res_raw;
                write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
                write.dstArrayElement = 0;
                write.dstBinding = binding;
                write.descriptorCount = 1;
            }
            else
            {
                // shader_view_dimension::textureX

                auto& img_info = image_infos.emplace_back();
                img_info.imageView = makeImageView(srv);
                img_info.imageLayout = util::to_image_layout(resource_state::shader_resource);
                img_info.sampler = nullptr;

                perform_write(srv_native_type, binding, true);
            }
        }

        if (sampler_configs.size() > 0)
        {
            new_node.samplers.reserve(sampler_configs.size());
            for (auto i = 0u; i < sampler_configs.size(); ++i)
            {
                auto const& sampler_conf = sampler_configs[i];
                new_node.samplers.push_back(makeSampler(sampler_conf));

                auto& img_info = image_infos.emplace_back();
                img_info.imageView = nullptr;
                img_info.imageLayout = util::to_image_layout(resource_state::shader_resource);
                img_info.sampler = new_node.samplers.back();

                perform_write(VK_DESCRIPTOR_TYPE_SAMPLER, spv::sampler_binding_start + i, true);
            }
        }

        vkUpdateDescriptorSets(mAllocator.getDevice(), uint32_t(writes.size()), writes.data(), 0, nullptr);

        // Store image views in the new node
        // These are allocating containers, however they are not accessed in a hot path
        // We only do this because they have to stay alive until this shader view is freed
        new_node.image_views.reserve(image_infos.size());
        for (auto const& img_info : image_infos)
        {
            new_node.image_views.push_back(img_info.imageView);
        }
    }

    return {static_cast<handle::handle_t>(pool_index)};
}

void phi::vk::ShaderViewPool::free(phi::handle::shader_view sv)
{
    // TODO: dangle check

    shader_view_node& freed_node = mPool.get(sv._value);
    internalFree(freed_node);

    {
        // This is a write access to the pool and allocator, and must be synced
        auto lg = std::lock_guard(mMutex);
        mAllocator.free(freed_node.raw_desc_set);
        mPool.release(sv._value);
    }
}

void phi::vk::ShaderViewPool::free(cc::span<const phi::handle::shader_view> svs)
{
    // This is a write access to the pool and allocator, and must be synced
    auto lg = std::lock_guard(mMutex);
    for (auto sv : svs)
    {
        shader_view_node& freed_node = mPool.get(sv._value);
        internalFree(freed_node);
        mAllocator.free(freed_node.raw_desc_set);
        mPool.release(sv._value);
    }
}

void phi::vk::ShaderViewPool::initialize(VkDevice device, ResourcePool* res_pool, unsigned num_cbvs, unsigned num_srvs, unsigned num_uavs, unsigned num_samplers)
{
    mDevice = device;
    mResourcePool = res_pool;

    mAllocator.initialize(mDevice, num_cbvs, num_srvs, num_uavs, num_samplers);
    // Due to the fact that each shader argument represents up to one CBV, this is the upper limit for the amount of shader_view handles
    mPool.initialize(num_cbvs);
}

void phi::vk::ShaderViewPool::destroy()
{
    auto num_leaks = 0;
    mPool.iterate_allocated_nodes([&](shader_view_node& leaked_node) {
        ++num_leaks;

        internalFree(leaked_node);
        mAllocator.free(leaked_node.raw_desc_set);
    });

    if (num_leaks > 0)
    {
        PHI_LOG("leaked {} handle::shader_view object{}", num_leaks, num_leaks == 1 ? "" : "s");
    }

    mAllocator.destroy();
}

VkImageView phi::vk::ShaderViewPool::makeImageView(const resource_view& sve, bool is_uav) const
{
    VkImageViewCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = mResourcePool->getRawImage(sve.resource);
    info.viewType = util::to_native_image_view_type(sve.dimension);

    info.format = util::to_vk_format(sve.pixel_format);
    info.subresourceRange.aspectMask = util::to_native_image_aspect(sve.pixel_format);
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

    VkImageView res;
    auto const vr = vkCreateImageView(mDevice, &info, nullptr, &res);
    CC_ASSERT(vr == VK_SUCCESS);
    return res;
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
    info.maxAnisotropy = static_cast<float>(config.max_anisotropy);
    info.borderColor = util::to_native(config.border_color);
    info.compareEnable = config.compare_func != sampler_compare_func::disabled ? VK_TRUE : VK_FALSE;
    info.compareOp = util::to_native(config.compare_func);

    VkSampler res;
    PHI_VK_VERIFY_SUCCESS(vkCreateSampler(mDevice, &info, nullptr, &res));
    return res;
}

void phi::vk::ShaderViewPool::internalFree(phi::vk::ShaderViewPool::shader_view_node& node) const
{
    // Destroy the contained image views
    for (auto const iv : node.image_views)
    {
        vkDestroyImageView(mDevice, iv, nullptr);
    }
    node.image_views.clear();

    // Destroy the contained samplers
    for (auto const s : node.samplers)
    {
        vkDestroySampler(mDevice, s, nullptr);
    }
    node.samplers.clear();

    // destroy the descriptor set layout used for creation
    vkDestroyDescriptorSetLayout(mDevice, node.raw_desc_set_layout, nullptr);
}
