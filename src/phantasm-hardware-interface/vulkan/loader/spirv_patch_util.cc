#include "spirv_patch_util.hh"

#include <algorithm>
#include <fstream>

#include <clean-core/alloc_array.hh>
#include <clean-core/array.hh>
#include <clean-core/assert.hh>
#include <clean-core/bit_cast.hh>
#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/common/lib/SPIRV_reflect/spirv_reflect.h>

#include <phantasm-hardware-interface/common/byte_reader.hh>
#include <phantasm-hardware-interface/common/container/unique_buffer.hh>
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
    case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
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

    case SPV_REFLECT_SHADER_STAGE_ANY_HIT_BIT_NV:
        return sd::ray_any_hit;
    case SPV_REFLECT_SHADER_STAGE_RAYGEN_BIT_NV:
        return sd::ray_gen;
    case SPV_REFLECT_SHADER_STAGE_CLOSEST_HIT_BIT_NV:
        return sd::ray_closest_hit;
    case SPV_REFLECT_SHADER_STAGE_CALLABLE_BIT_NV:
        return sd::ray_callable;
    case SPV_REFLECT_SHADER_STAGE_INTERSECTION_BIT_NV:
        return sd::ray_intersect;
    case SPV_REFLECT_SHADER_STAGE_MISS_BIT_NV:
        return sd::ray_miss;
    }
    CC_UNREACHABLE_SWITCH_WORKAROUND(shader_stage_flags);
}

VkShaderStageFlags reflect_to_native_shader_stage(SpvReflectShaderStageFlagBits shader_stage_flags)
{
    switch (shader_stage_flags)
    {
        // graphics and compute stages map 1:1 to the native enum
    case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
    case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
    case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
    case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT:
    case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
    case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
        return shader_stage_flags;

        // spirv-reflect only returns the shader stage of the first entrypoint of a raytracing library
        // but the descriptors are (potentially) visible to all stages, so return a mask over all
        // (we do not need special handling for the pipeline stage flags because there's just a single ray tracing stage)
    case SPV_REFLECT_SHADER_STAGE_ANY_HIT_BIT_NV:
    case SPV_REFLECT_SHADER_STAGE_RAYGEN_BIT_NV:
    case SPV_REFLECT_SHADER_STAGE_CLOSEST_HIT_BIT_NV:
    case SPV_REFLECT_SHADER_STAGE_CALLABLE_BIT_NV:
    case SPV_REFLECT_SHADER_STAGE_INTERSECTION_BIT_NV:
    case SPV_REFLECT_SHADER_STAGE_MISS_BIT_NV:
        return VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_ANY_HIT_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV | VK_SHADER_STAGE_MISS_BIT_NV | VK_SHADER_STAGE_INTERSECTION_BIT_NV | VK_SHADER_STAGE_CALLABLE_BIT_NV;
    }
    CC_UNREACHABLE("untranslated shader stage");
}

void patchSpvReflectShader(SpvReflectShaderModule& module,
                           cc::alloc_vector<phi::vk::util::spirv_desc_info>& out_desc_infos,
                           cc::allocator* scratch_alloc,
                           VkShaderStageFlags visible_shader_stages,
                           VkPipelineStageFlags visible_pipeline_stages)
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
            new_info.visible_stage = visible_shader_stages;
            new_info.visible_pipeline_stage = visible_pipeline_stages;
        }
    }

    return;
}


constexpr unsigned gc_patched_spirv_binary_version = 0xDEAD0001;
}

phi::vk::util::patched_spirv_stage phi::vk::util::create_patched_spirv(std::byte const* bytecode, size_t bytecode_size, spirv_refl_info& out_info, cc::allocator* scratch_alloc)
{
    patched_spirv_stage res;

    SpvReflectShaderModule module;
    auto const result = spvReflectCreateShaderModule(bytecode_size, bytecode, &module);
    CC_ASSERT(result == SPV_REFLECT_RESULT_SUCCESS && "failed to reflect SPIR-V");
    
    res.stage = reflect_to_pr(module.shader_stage);

    VkShaderStageFlags const native_shader_flags = reflect_to_native_shader_stage(module.shader_stage);
    VkPipelineStageFlags const native_pipeline_flags = util::to_pipeline_stage_flags(res.stage);

    patchSpvReflectShader(module, out_info.descriptor_infos, scratch_alloc, native_shader_flags, native_pipeline_flags);

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
    spirv_desc_info* current_descriptor = nullptr;

    for (auto const& descriptor : desc_infos)
    {
        if (current_descriptor &&                             // this is not the first range
            current_descriptor->set == descriptor.set &&      // set and -
            current_descriptor->binding == descriptor.binding // - binding are the same as current range
        )
        {
            CC_ASSERT(current_descriptor->type == descriptor.type && "SPIR-V descriptor type overlap detected");
            CC_ASSERT(current_descriptor->binding_array_size == descriptor.binding_array_size && "SPIR-V descriptor array mismatch detected");

            // this descriptor is the same as the previous one, just as seen from a different entrypoint,
            // bit-OR the shader stage bits
            current_descriptor->visible_stage |= descriptor.visible_stage;
            current_descriptor->visible_pipeline_stage |= descriptor.visible_pipeline_stage;
        }
        else
        {
            // this element is a different descriptor, advance
            sorted_merged_res.push_back(descriptor);
            current_descriptor = &sorted_merged_res.back();
        }
    }

    // change all the CBVs to UNIFORM_BUFFER_DYNAMIC
    for (auto& range : sorted_merged_res)
    {
        // set: CBVs are in up-shifted sets {4, 5, 6, 7}
        // type: uniform buffers cannot be dynamically (at draw time) switched
        if (range.set >= limits::max_shader_arguments && range.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        {
            // The CBV is always in b0
            CC_ASSERT(range.binding == spv::cbv_binding_start && "invalid uniform buffer descriptor outside b0 in reflection");
            range.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        }
    }

    return sorted_merged_res;
}

bool phi::vk::util::is_consistent_with_reflection(cc::span<const phi::vk::util::spirv_desc_info> reflected_descriptors, phi::arg::shader_arg_shapes arg_shapes)
{
    struct reflected_range_infos
    {
        unsigned num_cbvs = 0;
        unsigned num_srvs = 0;
        unsigned num_uavs = 0;
        unsigned num_samplers = 0;
    };

    cc::array<reflected_range_infos, limits::max_shader_arguments> range_infos;

    for (auto const& descriptor : reflected_descriptors)
    {
        auto set_shape_index = descriptor.set;

        // wrap CBVs down to their "true" set (as it is given in HLSL)
        if (set_shape_index >= limits::max_shader_arguments)
            set_shape_index -= limits::max_shader_arguments;

        reflected_range_infos& info = range_infos[set_shape_index];

        if (descriptor.binding >= spv::sampler_binding_start)
        {
            // Sampler
            info.num_samplers = cc::max(info.num_samplers, 1 + (descriptor.binding - spv::sampler_binding_start));
        }
        else if (descriptor.binding >= spv::uav_binding_start)
        {
            // UAV
            info.num_uavs = cc::max(info.num_uavs, 1 + (descriptor.binding - spv::uav_binding_start));
        }
        else if (descriptor.binding >= spv::srv_binding_start)
        {
            // SRV
            info.num_srvs = cc::max(info.num_srvs, 1 + (descriptor.binding - spv::srv_binding_start));
        }
        else /*if (range.binding >= spv::cbv_binding_start)*/
        {
            // CBV
            info.num_cbvs = cc::max(info.num_cbvs, 1 + (descriptor.binding - spv::cbv_binding_start));
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
    log_obj("SPIR-V descriptor info:\n");
    for (auto const& i : info)
    {
        log_obj.printf("  set %u, binding %u, array size %u, VkDescriptorType %d\n", i.set, i.binding, i.binding_array_size, i.type);
    }
}

bool phi::vk::util::write_patched_spirv(const phi::vk::util::patched_spirv_stage& spirv,
                                        cc::span<const phi::vk::util::spirv_desc_info> merged_descriptor_info,
                                        bool has_root_consts,
                                        const char* out_path)
{
    if (!out_path)
        return false;

    auto outfile = std::fstream(out_path, std::ios::out | std::ios::binary);
    if (!outfile.good())
        return false;

    outfile.write((char const*)&gc_patched_spirv_binary_version, sizeof(gc_patched_spirv_binary_version));

    // write patched SPIR-V
    outfile.write((char const*)&spirv.size, sizeof(spirv.size)); // size of patched SPIR-V
    outfile.write((char const*)spirv.data, spirv.size);          // patched SPIR-V

    // write entrypoint string
    size_t const entrypoint_size = spirv.entrypoint_name.size();
    outfile.write((char const*)&entrypoint_size, sizeof(entrypoint_size)); // size of entrypoint string
    outfile.write(spirv.entrypoint_name.c_str(), entrypoint_size);         // entrypoint string

    // write shader stage
    outfile.write((char const*)&spirv.stage, sizeof(spirv.stage));

    // write descriptor infos
    size_t const num_descriptor_infos = merged_descriptor_info.size();
    outfile.write((char const*)&num_descriptor_infos, sizeof(num_descriptor_infos));                // num of descriptor infos
    outfile.write((char const*)merged_descriptor_info.data(), merged_descriptor_info.size_bytes()); // descriptor info data

    // has root constants
    outfile.write((char const*)&has_root_consts, sizeof(bool));

    return true;
}

bool phi::vk::util::parse_patched_spirv(cc::span<const std::byte> data, phi::vk::util::patched_spirv_data_nonowning& out_parsed)
{
    if (data.empty())
        return false;

    auto reader = byte_reader{data};

    unsigned version_number = 0;
    reader.read_t(version_number);

    if (version_number != gc_patched_spirv_binary_version)
        return false;

    // read patched SPIR-V
    reader.read_t(out_parsed.binary_size_bytes);
    out_parsed.binary_data = reader.head();
    reader.skip(out_parsed.binary_size_bytes);

    // read entrypoint string
    size_t string_length = 0;
    reader.read_t(string_length);
    out_parsed.entrypoint_name = reinterpret_cast<char const*>(reader.head());
    reader.skip(string_length);

    // read shader stage
    reader.read_t(out_parsed.stage);

    // read descriptor infos
    size_t num_descriptor_infos = 0u;
    reader.read_t(num_descriptor_infos);
    out_parsed.descriptor_infos = {reinterpret_cast<spirv_desc_info const*>(reader.head()), num_descriptor_infos};
    reader.skip(out_parsed.descriptor_infos.size_bytes());

    // read root constant flag
    reader.read_t(out_parsed.has_root_constants);

    return true;
}
