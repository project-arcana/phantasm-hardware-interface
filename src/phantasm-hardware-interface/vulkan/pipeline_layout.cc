#include "pipeline_layout.hh"

#include <iostream>

#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/resources/descriptor_allocator.hh>
#include <phantasm-hardware-interface/vulkan/resources/transition_barrier.hh>

void pr::backend::vk::detail::pipeline_layout_params::descriptor_set_params::add_descriptor(VkDescriptorType type, unsigned binding, unsigned array_size, VkShaderStageFlagBits visibility)
{
    VkDescriptorSetLayoutBinding& new_binding = bindings.emplace_back();
    new_binding = {};
    new_binding.binding = binding;
    new_binding.descriptorType = type;
    new_binding.descriptorCount = array_size;

    // TODO: We have access to precise visibility constraints _in this function_, in the `visibility` argument
    // however, in shader_view_pool, DescriptorSets must be created without this knowledge, which is why
    // the pool falls back to VK_SHADER_STAGE_ALL_GRAPHICS for its temporary layouts. And since the descriptors would
    // be incompatible, we have to use the same thing here
    new_binding.stageFlags = (visibility == VK_SHADER_STAGE_COMPUTE_BIT) ? visibility : VK_SHADER_STAGE_ALL_GRAPHICS;
    new_binding.pImmutableSamplers = nullptr; // Optional
}

void pr::backend::vk::detail::pipeline_layout_params::descriptor_set_params::fill_in_samplers(cc::span<VkSampler const> samplers)
{
    for (auto& binding : bindings)
    {
        if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER)
        {
            binding.pImmutableSamplers = samplers.data();
            return;
        }
    }

    CC_ASSERT(false && "Failed to fill in samplers - not present in shader");
}

VkDescriptorSetLayout pr::backend::vk::detail::pipeline_layout_params::descriptor_set_params::create_layout(VkDevice device) const
{
    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = uint32_t(bindings.size());
    layout_info.pBindings = bindings.data();


    VkDescriptorSetLayout res;
    PR_VK_VERIFY_SUCCESS(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &res));

    return res;
}

void pr::backend::vk::pipeline_layout::initialize(VkDevice device, cc::span<const util::spirv_desc_info> range_infos, bool add_push_constants)
{
    detail::pipeline_layout_params params;
    params.initialize_from_reflection_info(range_infos);

    // copy pipeline stage visibilities
    descriptor_set_visibilities = params.merged_pipeline_visibilities;

    for (auto const& param_set : params.descriptor_sets)
    {
        descriptor_set_layouts.push_back(param_set.create_layout(device));
    }

    // always create a push constant range (which is conditionally set in the layout info)
    VkPushConstantRange pushconst_range = {};

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = uint32_t(descriptor_set_layouts.size());
    layout_info.pSetLayouts = descriptor_set_layouts.data();

    if (add_push_constants)
    {
        // detect if this is a compute shader
        bool is_compute = true;
        for (auto const vis : descriptor_set_visibilities)
        {
            if (!(vis & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) && vis != 0)
            {
                is_compute = false;
                break;
            }
        }

        // populate the push constant range accordingly
        pushconst_range.size = limits::max_root_constant_bytes;
        pushconst_range.offset = 0;
        pushconst_range.stageFlags = is_compute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS;

        this->push_constant_stages = pushconst_range.stageFlags;

        // refer to it in the layout info
        layout_info.pushConstantRangeCount = 1u;
        layout_info.pPushConstantRanges = &pushconst_range;
    }
    else
    {
        this->push_constant_stages = VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM;
    }

    PR_VK_VERIFY_SUCCESS(vkCreatePipelineLayout(device, &layout_info, nullptr, &raw_layout));
}

void pr::backend::vk::pipeline_layout::print() const
{
    std::cout << "[pr][backend][vk] pipeline_layout:" << std::endl;
    std::cout << "  " << descriptor_set_layouts.size() << " descriptor set layouts, " << descriptor_set_visibilities.size() << " visibilities" << std::endl;
    std::cout << "  raw layout: " << raw_layout << ", has push consts: " << (has_push_constants() ? "yes" : "no") << std::endl;
}

void pr::backend::vk::pipeline_layout::free(VkDevice device)
{
    for (auto const layout : descriptor_set_layouts)
        vkDestroyDescriptorSetLayout(device, layout, nullptr);

    vkDestroyPipelineLayout(device, raw_layout, nullptr);
}

void pr::backend::vk::detail::pipeline_layout_params::initialize_from_reflection_info(cc::span<const util::spirv_desc_info> reflection_info)
{
    auto const add_set = [this]() {
        descriptor_sets.emplace_back();
        merged_pipeline_visibilities.push_back(0);
    };

    add_set();
    for (auto const& desc : reflection_info)
    {
        auto const set_delta = desc.set - (descriptor_sets.size() - 1);
        if (set_delta == 1)
        {
            // the next set has been reached
            add_set();
        }
        else if (set_delta > 1)
        {
            // some sets have been skipped
            for (auto i = 0u; i < set_delta; ++i)
                add_set();
        }

        descriptor_sets.back().add_descriptor(desc.type, desc.binding, desc.binding_array_size, desc.visible_stage);
        merged_pipeline_visibilities.back() |= desc.visible_pipeline_stage;
    }
}
