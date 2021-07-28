#include "pipeline_layout.hh"

#include <phantasm-hardware-interface/common/log.hh>

#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/resources/descriptor_allocator.hh>
#include <phantasm-hardware-interface/vulkan/resources/transition_barrier.hh>

void phi::vk::detail::pipeline_layout_params::descriptor_set_params::add_descriptor(VkDescriptorType type, uint32_t binding, uint32_t array_size, VkShaderStageFlags visibility)
{
    VkDescriptorSetLayoutBinding& new_binding = bindings.emplace_back();
    new_binding = {};
    new_binding.binding = binding;
    new_binding.descriptorType = type;
    new_binding.descriptorCount = array_size;

    enum : VkShaderStageFlags
    {
        mask_all_raytracing = VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_ANY_HIT_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV
                              | VK_SHADER_STAGE_MISS_BIT_NV | VK_SHADER_STAGE_INTERSECTION_BIT_NV | VK_SHADER_STAGE_CALLABLE_BIT_NV,
        mask_all_graphics = VK_SHADER_STAGE_ALL_GRAPHICS
    };

    if (visibility == VK_SHADER_STAGE_COMPUTE_BIT)
    {
        new_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    else if (visibility & mask_all_graphics)
    {
        // NOTE: We have access to precise visibility constraints at PSO creation time (via SPIR-V reflection)
        // however, at shader_view creation, DescriptorSets must be created without this knowledge (only whether it's compute or graphics)
        // thus the pool falls back to VK_SHADER_STAGE_ALL_GRAPHICS for its temporary layouts - and for compatibility, these must match
        new_binding.stageFlags = mask_all_graphics;
    }
    else if (visibility & mask_all_raytracing)
    {
        // there are no shader_views for raytracing shaders,
        // thus we do not have to make this more coarse
        new_binding.stageFlags = visibility;
    }
    else
    {
        CC_ASSERT(false && "unexpected descriptor shader visibility");
        new_binding.stageFlags = visibility;
    }

    new_binding.pImmutableSamplers = nullptr; // Optional
}

void phi::vk::detail::pipeline_layout_params::descriptor_set_params::fill_in_immutable_samplers(cc::span<VkSampler const> samplers)
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

VkDescriptorSetLayout phi::vk::detail::pipeline_layout_params::descriptor_set_params::create_layout(VkDevice device) const
{
    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = uint32_t(bindings.size());
    layout_info.pBindings = bindings.data();

    VkDescriptorSetLayout res;
    PHI_VK_VERIFY_SUCCESS(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &res));

    return res;
}

void phi::vk::pipeline_layout::initialize(VkDevice device, cc::span<const util::ReflectedDescriptorInfo> descriptor_info, bool add_push_constants)
{
    // partition the descriptors into their sets
    detail::pipeline_layout_params params;
    params.initialize_from_reflection_info(descriptor_info);

    // copy pipeline stage visibilities
    descriptor_set_visibilities = params.merged_pipeline_visibilities;

    // create the descriptor sets
    for (auto const& param_set : params.descriptor_sets)
    {
        descriptor_set_layouts.push_back(param_set.create_layout(device));
    }

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

    // create the layout
    PHI_VK_VERIFY_SUCCESS(vkCreatePipelineLayout(device, &layout_info, nullptr, &raw_layout));
}

void phi::vk::pipeline_layout::print() const
{
    PHI_LOG << "pipeline_layout:\n"
               "  "
            << descriptor_set_layouts.size() << " descriptor set layouts, " << descriptor_set_visibilities.size()
            << " visibilities\n"
               "  raw layout: "
            << raw_layout << ", has push consts: " << (has_push_constants() ? "yes" : "no");
}

void phi::vk::pipeline_layout::free(VkDevice device)
{
    for (VkDescriptorSetLayout const layout : descriptor_set_layouts)
        vkDestroyDescriptorSetLayout(device, layout, nullptr);

    vkDestroyPipelineLayout(device, raw_layout, nullptr);
}

void phi::vk::detail::pipeline_layout_params::initialize_from_reflection_info(cc::span<const util::ReflectedDescriptorInfo> reflection_info)
{
    auto f_add_set = [this]() {
        descriptor_sets.emplace_back();
        merged_pipeline_visibilities.push_back(0);
    };

    f_add_set();

    // iterate over the descriptors
    // these are sorted and deduplicated/merged
    for (util::ReflectedDescriptorInfo const& desc : reflection_info)
    {
        size_t const set_delta = desc.set - (descriptor_sets.size() - 1);

        if (set_delta == 1)
        {
            // the next set has been reached
            f_add_set();
        }
        else if (set_delta > 1)
        {
            // some sets have been skipped
            for (auto i = 0u; i < set_delta; ++i)
                f_add_set();
        }

        // add the descriptor to this set
        descriptor_sets.back().add_descriptor(desc.type, desc.binding, desc.binding_array_size, desc.visible_stage);
        // merge the visibility flags
        merged_pipeline_visibilities.back() |= desc.visible_pipeline_stage;
    }
}
