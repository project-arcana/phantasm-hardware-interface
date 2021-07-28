#include "spirv_patch_util.hh"

#include <cstdio>

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
constexpr VkDescriptorType reflect_to_native(SpvReflectDescriptorType type)
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

constexpr phi::shader_stage reflect_to_pr(SpvReflectShaderStageFlagBits shader_stage_flags)
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

constexpr bool isBindingCBV(uint32_t binding) { return binding >= phi::vk::spv::cbv_binding_start && binding < phi::vk::spv::srv_binding_start; }

constexpr bool isBindingSRV(uint32_t binding) { return binding >= phi::vk::spv::srv_binding_start && binding < phi::vk::spv::uav_binding_start; }

constexpr bool isBindingUAV(uint32_t binding) { return binding >= phi::vk::spv::uav_binding_start && binding < phi::vk::spv::sampler_binding_start; }

constexpr bool isBindingSampler(uint32_t binding) { return binding >= phi::vk::spv::sampler_binding_start; }

constexpr bool isDescriptorSetInNthArgument(uint32_t set, uint32_t argument)
{
    return (set < phi::limits::max_shader_arguments && set == argument) || //
           (set >= phi::limits::max_shader_arguments && set - phi::limits::max_shader_arguments == argument);
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
        return VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_ANY_HIT_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV | VK_SHADER_STAGE_MISS_BIT_NV
               | VK_SHADER_STAGE_INTERSECTION_BIT_NV | VK_SHADER_STAGE_CALLABLE_BIT_NV;
    }
    CC_UNREACHABLE("untranslated shader stage");
}

void patchSpvReflectShader(SpvReflectShaderModule& module,
                           cc::alloc_vector<phi::vk::util::ReflectedDescriptorInfo>& out_desc_infos,
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

struct ReflectedRangeInfos
{
    uint32_t num_cbvs = 0;
    uint32_t num_srvs = 0;
    uint32_t num_uavs = 0;
    uint32_t num_samplers = 0;
};

void descriptorsToRangeInfos(cc::span<phi::vk::util::ReflectedDescriptorInfo const> reflectedDescriptors,
                             ReflectedRangeInfos (&outRangeInfos)[phi::limits::max_shader_arguments])
{
    using namespace phi;
    using namespace phi::vk;

    for (auto const& descriptor : reflectedDescriptors)
    {
        auto set_shape_index = descriptor.set;

        // wrap CBVs down to their "true" set (as it is given in HLSL)
        if (set_shape_index >= limits::max_shader_arguments)
            set_shape_index -= limits::max_shader_arguments;

        CC_ASSERT(set_shape_index < phi::limits::max_shader_arguments && "Descriptor set index OOB (specified space beyond limits::max_shader_arguments?)");
        ReflectedRangeInfos& info = outRangeInfos[set_shape_index];

        if (descriptor.binding >= spv::sampler_binding_start)
        {
            // Sampler
            info.num_samplers += descriptor.binding_array_size;
        }
        else if (descriptor.binding >= spv::uav_binding_start)
        {
            // UAV
            info.num_uavs += descriptor.binding_array_size;
        }
        else if (descriptor.binding >= spv::srv_binding_start)
        {
            // SRV
            info.num_srvs += descriptor.binding_array_size;
        }
        else /*if (range.binding >= spv::cbv_binding_start)*/
        {
            // CBV
            info.num_cbvs += descriptor.binding_array_size;
        }
    }
}

// sort by set, then binding (both ascending)
void sortDescriptorsBySetAndBinding(cc::span<phi::vk::util::ReflectedDescriptorInfo> inOutDescriptorInfos)
{
    using namespace phi::vk::util;

    std::sort(inOutDescriptorInfos.begin(), inOutDescriptorInfos.end(), [](ReflectedDescriptorInfo const& lhs, ReflectedDescriptorInfo const& rhs) {
        if (lhs.set != rhs.set)
            return lhs.set < rhs.set;
        else
            return lhs.binding < rhs.binding;
    });
}

constexpr uint32_t gc_patched_spirv_binary_version = 0xDEAD0001;
} // namespace

phi::vk::util::PatchedShaderStage phi::vk::util::createPatchedShader(std::byte const* bytecode, size_t bytecode_size, ReflectedShaderInfo& out_info, cc::allocator* scratch_alloc)
{
    // we have to shift all CBVs up by [max num shader args] sets to make our API work in vulkan
    // unlike the register-to-binding shift with -fvk-[x]-shift, this cannot be done with DXC flags
    // instead we provide these helpers which use the spirv-reflect library to do the same

    PatchedShaderStage res;

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

    // copy name
    std::snprintf(res.entrypoint_name, sizeof(res.entrypoint_name), "%s", module.entry_point_name);

    // spirv-reflect internally checks if this field is a nullptr before calling ::free,
    // so we can keep it alive by setting this
    module._internal->spirv_code = nullptr;
    spvReflectDestroyShaderModule(&module);
    return res;
}


void phi::vk::util::freePatchedShader(const PatchedShaderStage& val)
{
    // do the same thing spirv-reflect would have done in spvReflectDestroyShaderModule
    ::free(val.data);
}

cc::alloc_vector<phi::vk::util::ReflectedDescriptorInfo> phi::vk::util::mergeReflectedDescriptors(cc::span<ReflectedDescriptorInfo> inOutDescriptorInfos,
                                                                                                  cc::allocator* alloc)
{
    sortDescriptorsBySetAndBinding(inOutDescriptorInfos);

    cc::alloc_vector<ReflectedDescriptorInfo> sorted_merged_res(alloc);
    sorted_merged_res.reserve(inOutDescriptorInfos.size());
    ReflectedDescriptorInfo* current_descriptor = nullptr;

    for (auto const& descriptor : inOutDescriptorInfos)
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

size_t phi::vk::util::addDummyDescriptors(cc::span<arg::shader_arg_shape const> argShapes, cc::alloc_vector<ReflectedDescriptorInfo>& inOutFillerDescriptors)
{
    // NOTE: this is near-impossible
    // not only does validation cry when the dummy descriptor type is not the SAME one as in the bound descriptor set,
    // it also changes the rendered image meaning this is proper, real UB
    // other open questions: visibility flags, array sizes
    //
    // we can probably do this for CBVs and Samplers (if solving visibility questions)
    // but for SRVs and UAVs it's not really an option


    // compute amounts in reflected descriptors
    ReflectedRangeInfos range_infos[limits::max_shader_arguments] = {};
    descriptorsToRangeInfos(inOutFillerDescriptors, range_infos);

    size_t numWritten = 0;
    auto F_AddDescriptor = [&](VkDescriptorType type, uint32_t set, uint32_t binding) {
        // TODO: visibility flags are arbitrary, might cause problems (ALL_GRAPHICS works for naive graphics PSOs)
        inOutFillerDescriptors.push_back(ReflectedDescriptorInfo{set, binding, 1, type, VK_SHADER_STAGE_ALL_GRAPHICS, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT});
        ++numWritten;
    };

    size_t const numDescriptorsBefore = inOutFillerDescriptors.size();

    VkDescriptorType const dummyTypeCBV = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; // correct
    VkDescriptorType const dummyTypeSRV = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;          // poor guess
    VkDescriptorType const dummyTypeUAV = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         // poor guess
    VkDescriptorType const dummyTypeSampler = VK_DESCRIPTOR_TYPE_SAMPLER;            // correct

    // go over each argument
    for (auto i = 0u; i < argShapes.size(); ++i)
    {
        auto const& ri = range_infos[i];
        auto const& shape = argShapes[i];

        uint32_t const shape_num_cbvs = shape.has_cbv ? 1u : 0u;

        if (ri.num_cbvs < shape_num_cbvs)
        {
            // add a dummy CBV in the single binding it can fit
            F_AddDescriptor(dummyTypeCBV, i + limits::max_shader_arguments, spv::cbv_binding_start);
        }

        if (ri.num_srvs < shape.num_srvs)
        {
            // add dummy SRVs, first fill up holes in the binding sequence, then append

            uint32_t numMissingSRVs = shape.num_srvs - ri.num_srvs;
            uint32_t currentBinding = spv::srv_binding_start;

            for (auto j = 0u; j < numDescriptorsBefore; ++j)
            {
                ReflectedDescriptorInfo const& descInfo = inOutFillerDescriptors[j];

                if (descInfo.set < i)
                {
                    // skip forward to set
                    continue;
                }
                else if (descInfo.set > i)
                {
                    // stop search beyond set
                    break;
                }

                if (isBindingCBV(descInfo.binding))
                {
                    // skip forward to SRVs
                }
                else if (!isBindingSRV(descInfo.binding))
                {
                    // stop search beyond SRVs
                    break;
                }

                // we are in the correct set, in SRVs
                if (currentBinding == descInfo.binding)
                {
                    // this binding continues seamlessly, skip
                    ++currentBinding;
                    continue;
                }

                // this binding has skipped
                uint32_t const numBindingsSkipped = descInfo.binding - currentBinding;
                uint32_t const numDummiesToAdd = cc::min(numMissingSRVs, numBindingsSkipped);

                // add dummies
                for (auto k = 0u; k < numDummiesToAdd; ++k)
                {
                    F_AddDescriptor(dummyTypeSRV, i, currentBinding++);
                }

                numMissingSRVs -= numDummiesToAdd;
                if (numMissingSRVs == 0)
                    break;
            }

            // add remaining
            for (auto k = 0u; k < numMissingSRVs; ++k)
            {
                F_AddDescriptor(dummyTypeSRV, i, currentBinding++);
            }
        }

        // TODO: UAVs, Samplers
    }

    // if any descriptors were added, re-sort by set/binding (ascending)
    if (numWritten > 0)
    {
        sortDescriptorsBySetAndBinding(inOutFillerDescriptors);
    }

    return numWritten;
}

bool phi::vk::util::warnIfReflectionIsInconsistent(cc::span<const phi::vk::util::ReflectedDescriptorInfo> reflected_descriptors, phi::arg::shader_arg_shapes arg_shapes)
{
    ReflectedRangeInfos range_infos[limits::max_shader_arguments];
    descriptorsToRangeInfos(reflected_descriptors, range_infos);

    bool isInconsistent = false;

    for (auto i = 0u; i < arg_shapes.size(); ++i)
    {
        auto const& ri = range_infos[i];
        auto const& shape = arg_shapes[i];

        if (ri.num_cbvs != (shape.has_cbv ? 1 : 0))
        {
            PHI_LOG_WARN << "SPIR-V reflection inconsistent - CBVs: " << ri.num_cbvs << " reflected, vs " << (shape.has_cbv ? 1 : 0) << " in argument #" << i;
            isInconsistent = true;
        }

        if (ri.num_srvs != shape.num_srvs)
        {
            PHI_LOG_WARN << "SPIR-V reflection inconsistent - SRVs: " << ri.num_srvs << " reflected, vs " << shape.num_srvs << " in argument #" << i;
            isInconsistent = true;
        }
        if (ri.num_uavs != shape.num_uavs)
        {
            PHI_LOG_WARN << "SPIR-V reflection inconsistent - UAVs: " << ri.num_uavs << " reflected, vs " << shape.num_uavs << " in argument #" << i;
            isInconsistent = true;
        }
        if (ri.num_samplers != shape.num_samplers)
        {
            PHI_LOG_WARN << "SPIR-V reflection inconsistent - Samplers: " << ri.num_samplers << " reflected, vs " << shape.num_samplers << " in argument #" << i;
            isInconsistent = true;
        }
    }

    return isInconsistent;
}

void phi::vk::util::logReflectedDescriptors(cc::span<const phi::vk::util::ReflectedDescriptorInfo> info)
{
    auto log_obj = PHI_LOG;
    log_obj("SPIR-V descriptor info:\n");
    for (auto const& i : info)
    {
        log_obj.printf("  set %u, binding %u, array size %u, VkDescriptorType %d\n", i.set, i.binding, i.binding_array_size, i.type);
    }
}
