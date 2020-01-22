#include "descriptor_allocator.hh"

#include <clean-core/array.hh>
#include <clean-core/assert.hh>
#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/vulkan/Device.hh>
#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-hardware-interface/vulkan/pipeline_layout.hh>

namespace pr::backend::vk
{
void DescriptorAllocator::initialize(VkDevice device, uint32_t num_cbvs, uint32_t num_srvs, uint32_t num_uavs, uint32_t num_samplers)
{
    mDevice = device;

    cc::capped_vector<VkDescriptorPoolSize, 6> type_sizes;

    if (num_cbvs > 0)
        type_sizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, num_cbvs});

    if (num_samplers > 0)
        type_sizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, num_samplers});

    if (num_srvs > 0)
    {
        // SRV-only types
        type_sizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, num_srvs});
        type_sizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, num_srvs});
    }

    if (num_uavs > 0)
    {
        // UAV-only types
        type_sizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, num_uavs});
    }

    if (num_srvs + num_uavs > 0)
    {
        // SRV or UAV types
        type_sizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, num_srvs + num_uavs});
    }

    VkDescriptorPoolCreateInfo descriptor_pool = {};
    descriptor_pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool.pNext = nullptr;
    descriptor_pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptor_pool.maxSets = num_srvs + num_uavs + num_cbvs + num_samplers;
    descriptor_pool.poolSizeCount = uint32_t(type_sizes.size());
    descriptor_pool.pPoolSizes = type_sizes.data();

    PR_VK_VERIFY_SUCCESS(vkCreateDescriptorPool(mDevice, &descriptor_pool, nullptr, &mPool));
}

void DescriptorAllocator::destroy() { vkDestroyDescriptorPool(mDevice, mPool, nullptr); }


VkDescriptorSet DescriptorAllocator::allocDescriptor(VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.pNext = nullptr;
    alloc_info.descriptorPool = mPool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    VkDescriptorSet res;
    PR_VK_VERIFY_SUCCESS(vkAllocateDescriptorSets(mDevice, &alloc_info, &res));
    return res;
}

void DescriptorAllocator::free(VkDescriptorSet descriptor_set) { vkFreeDescriptorSets(mDevice, mPool, 1, &descriptor_set); }

VkDescriptorSetLayout DescriptorAllocator::createSingleCBVLayout(bool usage_compute) const
{
    // NOTE: Eventually arguments could be constrained to stages
    auto const argument_visibility = usage_compute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS;

    cc::capped_vector<VkDescriptorSetLayoutBinding, 1> bindings;

    {
        VkDescriptorSetLayoutBinding& binding = bindings.emplace_back();
        binding = {};
        binding.binding = spv::cbv_binding_start; // CBV always in (0)
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        binding.descriptorCount = 1;
        binding.stageFlags = argument_visibility;
        binding.pImmutableSamplers = nullptr; // Optional
    }


    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = uint32_t(bindings.size());
    layout_info.pBindings = bindings.data();

    VkDescriptorSetLayout layout;
    PR_VK_VERIFY_SUCCESS(vkCreateDescriptorSetLayout(mDevice, &layout_info, nullptr, &layout));
    return layout;
}

VkDescriptorSetLayout DescriptorAllocator::createLayoutFromShaderViewArgs(cc::span<const shader_view_element> srvs,
                                                                          cc::span<const shader_view_element> uavs,
                                                                          unsigned num_samplers,
                                                                          bool usage_compute) const
{
    // NOTE: Eventually arguments could be constrained to stages in a more fine-grained manner
    auto const argument_visibility = usage_compute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS;

    detail::pipeline_layout_params::descriptor_set_params params;

    for (auto i = 0u; i < srvs.size(); ++i)
    {
        auto const native_type = util::to_native_srv_desc_type(srvs[i].dimension);
        params.add_descriptor(native_type, spv::srv_binding_start + i, 1, argument_visibility);
    }

    for (auto i = 0u; i < uavs.size(); ++i)
    {
        auto const native_type = util::to_native_uav_desc_type(uavs[i].dimension);
        params.add_descriptor(native_type, spv::uav_binding_start + i, 1, argument_visibility);
    }

    for (auto i = 0u; i < num_samplers; ++i)
    {
        params.add_descriptor(VK_DESCRIPTOR_TYPE_SAMPLER, spv::sampler_binding_start + i, 1, argument_visibility);
    }

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = uint32_t(params.bindings.size());
    layout_info.pBindings = params.bindings.data();

    VkDescriptorSetLayout layout;
    PR_VK_VERIFY_SUCCESS(vkCreateDescriptorSetLayout(mDevice, &layout_info, nullptr, &layout));

    return layout;
}
}
