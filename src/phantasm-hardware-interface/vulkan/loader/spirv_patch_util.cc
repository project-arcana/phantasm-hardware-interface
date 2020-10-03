#include "spirv_patch_util.hh"

#include <algorithm>

#include <clean-core/alloc_array.hh>
#include <clean-core/array.hh>
#include <clean-core/assert.hh>
#include <clean-core/bit_cast.hh>
#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/common/container/unique_buffer.hh>
#include <phantasm-hardware-interface/common/lib/spirv_reflect.hh>
#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/limits.hh>

#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace
{
[[nodiscard]] constexpr VkDescriptorType reflect_to_native(SpvReflectDescriptorType type)
{
    switch (type)
    {
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    }
    CC_UNREACHABLE_SWITCH_WORKAROUND(type);
}

[[nodiscard]] constexpr phi::shader_stage reflect_to_pr(SpvReflectShaderStageFlagBits shader_stage_flags)
{
    using sd = phi::shader_stage;
    switch (shader_stage_flags)
    {
    case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
        return sd::vertex;
    case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
        return sd::hull;
    case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
        return sd::domain;
    case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT:
        return sd::geometry;
    case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
        return sd::pixel;

    case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
        return sd::compute;

        //    case VK_SHADER_STAGE_ANY_HIT_BIT_NV:
        //        return sd::ray_any_hit;
        //    case VK_SHADER_STAGE_RAYGEN_BIT_NV:
        //        return sd::ray_gen;
        //    case VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV:
        //        return sd::ray_closest_hit;
        //    case VK_SHADER_STAGE_CALLABLE_BIT_NV:
        //        return sd::ray_callable;
        //    case VK_SHADER_STAGE_INTERSECTION_BIT_NV:
        //        return sd::ray_intersect;
        //    case VK_SHADER_STAGE_MISS_BIT_NV:
        //        return sd::ray_miss;
    }
    CC_UNREACHABLE_SWITCH_WORKAROUND(shader_stage_flags);
}

void patchSpvReflectShader(SpvReflectShaderModule& module,
                           phi::shader_stage current_stage,
                           cc::alloc_vector<phi::vk::util::spirv_desc_info>& out_desc_infos,
                           cc::allocator* scratch_alloc)
{
    using namespace phi::vk;

    // shift CBVs up by [max_shader_arguments] sets
    {
        uint32_t num_bindings;
        spvReflectEnumerateDescriptorBindings(&module, &num_bindings, nullptr);
        cc::alloc_array<SpvReflectDescriptorBinding*> bindings(num_bindings, scratch_alloc);
        spvReflectEnumerateDescriptorBindings(&module, &num_bindings, bindings.data());

        for (auto const* const b : bindings)
        {
            if (b->resource_type == SPV_REFLECT_RESOURCE_FLAG_CBV)
            {
                auto const new_set = b->set + phi::limits::max_shader_arguments;
                spvReflectChangeDescriptorBindingNumbers(&module, b, b->binding, new_set);
            }
        }
    }

    // push the found descriptors
    {
        uint32_t num_bindings;
        spvReflectEnumerateDescriptorBindings(&module, &num_bindings, nullptr);
        cc::alloc_array<SpvReflectDescriptorBinding*> bindings(num_bindings, scratch_alloc);
        spvReflectEnumerateDescriptorBindings(&module, &num_bindings, bindings.data());

        out_desc_infos.reserve(out_desc_infos.size() + num_bindings);

        for (auto const* const b : bindings)
        {
            auto& new_info = out_desc_infos.emplace_back();
            new_info.set = b->set;
            new_info.binding = b->binding;
            new_info.binding_array_size = b->count;
            new_info.type = reflect_to_native(b->descriptor_type);
            new_info.visible_stage = util::to_shader_stage_flags(current_stage);
            new_info.visible_pipeline_stage = util::to_pipeline_stage_flags(current_stage);
        }
    }

    return;
}

}

phi::vk::util::patched_spirv_stage phi::vk::util::create_patched_spirv(std::byte const* bytecode, size_t bytecode_size, spirv_refl_info& out_info, cc::allocator* scratch_alloc)
{
    patched_spirv_stage res;

    SpvReflectShaderModule module;
    auto const result = spvReflectCreateShaderModule(bytecode_size, bytecode, &module);
    CC_ASSERT(result == SPV_REFLECT_RESULT_SUCCESS && "failed to reflect SPIR-V");

    res.stage = reflect_to_pr(module.shader_stage);
    patchSpvReflectShader(module, res.stage, out_info.descriptor_infos, scratch_alloc);

    res.size = spvReflectGetCodeSize(&module);
    res.data = cc::bit_cast<std::byte*>(module._internal->spirv_code);

    // check for push constants
    {
        uint32_t num_blocks;
        spvReflectEnumeratePushConstantBlocks(&module, &num_blocks, nullptr);
        CC_ASSERT(num_blocks <= 1 && "more than one push constant block in reflection");

        if (num_blocks == 1)
        {
            out_info.has_push_constants = true;
        }
    }

    res.entrypoint_name = module.entry_point_name;

    // spirv-reflect internally checks if this field is a nullptr before calling ::free,
    // so we can keep it alive by setting this
    module._internal->spirv_code = nullptr;
    spvReflectDestroyShaderModule(&module);
    return res;
}


void phi::vk::util::free_patched_spirv(const patched_spirv_stage& val)
{
    // do the same thing spirv-reflect would have done in spvReflectDestroyShaderModule
    ::free(val.data);
}

cc::alloc_vector<phi::vk::util::spirv_desc_info> phi::vk::util::merge_spirv_descriptors(cc::span<spirv_desc_info> desc_infos, cc::allocator* alloc)
{
    // sort by set, then binding (both ascending)
    std::sort(desc_infos.begin(), desc_infos.end(), [](spirv_desc_info const& lhs, spirv_desc_info const& rhs) {
        if (lhs.set != rhs.set)
            return lhs.set < rhs.set;
        else
            return lhs.binding < rhs.binding;
    });

    cc::alloc_vector<spirv_desc_info> sorted_merged_res(alloc);
    sorted_merged_res.reserve(desc_infos.size());
    spirv_desc_info* curr_range = nullptr;

    for (auto const& di : desc_infos)
    {
        if (curr_range &&                     // not the first range
            curr_range->set == di.set &&      // set same as current range
            curr_range->binding == di.binding // binding same as current range
        )
        {
            CC_ASSERT(curr_range->type == di.type && "SPIR-V descriptor type overlap detected");
            CC_ASSERT(curr_range->binding_array_size == di.binding_array_size && "SPIR-V descriptor array mismatch detected");

            // this element mirrors the precursor, bit-OR the shader stage bits
            curr_range->visible_stage = static_cast<VkShaderStageFlagBits>(curr_range->visible_stage | di.visible_stage);
            curr_range->visible_pipeline_stage = static_cast<VkPipelineStageFlags>(curr_range->visible_pipeline_stage | di.visible_pipeline_stage);
        }
        else
        {
            sorted_merged_res.push_back(di);
            curr_range = &sorted_merged_res.back();
        }
    }

    // change the CBVs to UNIFORM_BUFFER_DYNAMIC
    for (auto& range : sorted_merged_res)
    {
        if (range.set >= limits::max_shader_arguments && range.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        {
            // The CBV is always in b0
            CC_ASSERT(range.binding == spv::cbv_binding_start && "invalid uniform buffer descriptor outside b0 in reflection");
            range.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        }
    }

    return sorted_merged_res;
}

bool phi::vk::util::is_consistent_with_reflection(cc::span<const phi::vk::util::spirv_desc_info> spirv_ranges, phi::arg::shader_arg_shapes arg_shapes)
{
    struct reflected_range_infos
    {
        unsigned num_cbvs = 0;
        unsigned num_srvs = 0;
        unsigned num_uavs = 0;
        unsigned num_samplers = 0;
    };

    cc::array<reflected_range_infos, limits::max_shader_arguments> range_infos;

    for (auto const& range : spirv_ranges)
    {
        auto set_shape_index = range.set;
        if (set_shape_index >= limits::max_shader_arguments)
            set_shape_index -= limits::max_shader_arguments;

        reflected_range_infos& info = range_infos[set_shape_index];

        if (range.binding >= spv::sampler_binding_start)
        {
            info.num_samplers = cc::max(info.num_samplers, 1 + (range.binding - spv::sampler_binding_start));
        }
        else if (range.binding >= spv::uav_binding_start)
        {
            info.num_uavs = cc::max(info.num_uavs, 1 + (range.binding - spv::uav_binding_start));
        }
        else if (range.binding >= spv::srv_binding_start)
        {
            info.num_srvs = cc::max(info.num_srvs, 1 + (range.binding - spv::srv_binding_start));
        }
        else /*if (range.binding >= spv::cbv_binding_start)*/
        {
            info.num_cbvs = cc::max(info.num_cbvs, 1 + (range.binding - spv::cbv_binding_start));
        }
    }

    for (auto i = 0u; i < arg_shapes.size(); ++i)
    {
        auto const& ri = range_infos[i];
        auto const& shape = arg_shapes[i];

        if (ri.num_cbvs != (shape.has_cbv ? 1 : 0))
        {
            PHI_LOG_ERROR << "SPIR-V reflection inconsistent - CBVs: " << ri.num_cbvs << " reflected, vs " << (shape.has_cbv ? 1 : 0) << " in argument #" << i;
            return false;
        }

        if (ri.num_srvs != shape.num_srvs)
        {
            PHI_LOG_ERROR << "SPIR-V reflection inconsistent - SRVs: " << ri.num_srvs << " reflected, vs " << shape.num_srvs << " in argument #" << i;
        }
        if (ri.num_uavs != shape.num_uavs)
        {
            PHI_LOG_ERROR << "SPIR-V reflection inconsistent - UAVs: " << ri.num_uavs << " reflected, vs " << shape.num_uavs << " in argument #" << i;
        }
        if (ri.num_samplers != shape.num_samplers)
        {
            PHI_LOG_ERROR << "SPIR-V reflection inconsistent - Samplers: " << ri.num_samplers << " reflected, vs " << shape.num_samplers
                          << " in argument #" << i;
        }
    }
    return true;
}

void phi::vk::util::print_spirv_info(cc::span<const phi::vk::util::spirv_desc_info> info)
{
    auto log_obj = PHI_LOG;
    log_obj << "SPIR-V descriptor info:\n";
    for (auto const& i : info)
    {
        log_obj << "  set " << i.set << ", binding " << i.binding << ", array size " << i.binding_array_size << ", VkDescriptorType " << i.type << '\n';
    }
}
