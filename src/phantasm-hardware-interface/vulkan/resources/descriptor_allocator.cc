#include "descriptor_allocator.hh"

#include <clean-core/array.hh>
#include <clean-core/assert.hh>
#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/vulkan/Device.hh>
#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-hardware-interface/vulkan/pipeline_layout.hh>

namespace phi::vk
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

    PHI_VK_VERIFY_SUCCESS(vkCreateDescriptorPool(mDevice, &descriptor_pool, nullptr, &mPool));
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
    PHI_VK_VERIFY_SUCCESS(vkAllocateDescriptorSets(mDevice, &alloc_info, &res));
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
    PHI_VK_VERIFY_SUCCESS(vkCreateDescriptorSetLayout(mDevice, &layout_info, nullptr, &layout));
    return layout;
}

VkDescriptorSetLayout DescriptorAllocator::createLayoutFromShaderViewArgs(cc::span<const resource_view> srvs,
                                                                          cc::span<const resource_view> uavs,
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
    PHI_VK_VERIFY_SUCCESS(vkCreateDescriptorSetLayout(mDevice, &layout_info, nullptr, &layout));

    return layout;
}
VkDescriptorSetLayout DescriptorAllocator::createLayoutFromDescription(arg::shader_view_description const& desc, bool usageCompute)
{
    // NOTE: Eventually arguments could be constrained to stages in a more fine-grained manner
    auto const argument_visibility = usageCompute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS;

    detail::pipeline_layout_params::descriptor_set_params params;

    uint32_t srvHead = 0u;
    uint32_t numSRVsInEntries = 0u;
    for (auto const& entry : desc.srv_entries)
    {
        auto const native_type = util::to_native_srv_desc_type(entry.category);

        params.add_descriptor(native_type, spv::srv_binding_start + srvHead, entry.array_size, argument_visibility);
        numSRVsInEntries += entry.array_size;
        ++srvHead;
    }

    CC_ASSERT_MSG(numSRVsInEntries == desc.num_srvs,
                  "Amount of SRVs specified does not match the sum of given SRV entries when creating an empty shader view\n"
                  "For the Vulkan backend, arg::shader_view_description::srv_entries is not optional");

    uint32_t uavHead = 0u;
    uint32_t numUAVsInEntries = 0u;
    for (auto const& entry : desc.uav_entries)
    {
        auto const native_type = util::to_native_uav_desc_type(entry.category);

        params.add_descriptor(native_type, spv::uav_binding_start + uavHead, entry.array_size, argument_visibility);
        numUAVsInEntries += entry.array_size;
        ++uavHead;
    }

    CC_ASSERT_MSG(numUAVsInEntries == desc.num_uavs,
                  "Amount of UAVs specified does not match the sum of given UAV entries when creating an empty shader view\n"
                  "For the Vulkan backend, arg::shader_view_description::uav_entries is not optional");

    for (auto i = 0u; i < desc.num_samplers; ++i)
    {
        params.add_descriptor(VK_DESCRIPTOR_TYPE_SAMPLER, spv::sampler_binding_start + i, 1, argument_visibility);
    }

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = uint32_t(params.bindings.size());
    layout_info.pBindings = params.bindings.data();

    VkDescriptorSetLayout layout;
    PHI_VK_VERIFY_SUCCESS(vkCreateDescriptorSetLayout(mDevice, &layout_info, nullptr, &layout));

    return layout;
}
} // namespace phi::vk
