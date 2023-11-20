#pragma once

#include <cstddef>
#include <cstdint>

#include <clean-core/alloc_vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/limits.hh>
#include <phantasm-hardware-interface/types.hh>

#include "volk.hh"

namespace phi::vk::spv
{
enum spv_constants_e : uint32_t
{
    // SPIR-V accepted by PHI Vulkan backends expects bindings by HLSL type as such:

    // CBVs (b): 0 - 999            - Starts first
    cbv_binding_start = 0u,

    // SRVs (t): 1000 - 1999        - Shifted by 1k
    srv_binding_start = 1000u,

    // UAVs (u): 2000 - 2999        - Shifted by 2k
    uav_binding_start = 2000u,

    // Samplers (s): 3000 - X       - Shifted by 3k
    sampler_binding_start = 3000u

    // This is assuming a HLSL -> SPIR-V path via DXC, and is done at shader compile time
    // using the -fvk-[x]-shift flags (see dxc-wrapper compiler.cc for the specific flags)

    // Additionally, in order to be able to create and update VkDescriptorSets independently
    // for A) handle::shader_view and B) the single one required for the CBV, we shift CBVs up in their _set_
    // So shader arguments map as follows to sets:
    // Arg 0 (space0)        SRV, UAV, Sampler: set 0,   CBV: set 4
    // Arg 1 (space1)        SRV, UAV, Sampler: set 1,   CBV: set 5
    // Arg 2 (space2)        SRV, UAV, Sampler: set 2,   CBV: set 6
    // Arg 3 (space3)        SRV, UAV, Sampler: set 3,   CBV: set 7
    // (this is required as there are no "root descriptors" in vulkan)

    // Unlike the binding offsets, these set shifts cannot be caused by DXC and must be patched post-compile
    // (currently always online, but this would eventually be part of asset bake)
    // this is currently done using spirv-reflect, see loader/spirv_patch_util for details
};
}

namespace phi::vk::util
{
/// info about a descriptor - where, what, and visibility
struct ReflectedDescriptorInfo
{
    uint32_t set;
    uint32_t binding;
    uint32_t binding_array_size;
    VkDescriptorType type;                       ///< type of the descriptor
    VkShaderStageFlags visible_stage;            ///< shaders it is visible to
    VkPipelineStageFlags visible_pipeline_stage; ///< pipeline stages it is visible to (only depends upon visible_stage)

    bool operator==(ReflectedDescriptorInfo const& rhs) const noexcept
    {
        static_assert(std::has_unique_object_representations_v<ReflectedDescriptorInfo>, "ReflectedDescriptorInfo cannot be memcmp'd");
        return std::memcmp(this, &rhs, sizeof(*this)) == 0;
        //        return set == rhs.set && binding == rhs.binding && binding_array_size == rhs.binding_array_size && type == rhs.type && visible_stage == rhs.visible_stage;
    }
};

// info about a shader stage: descriptor infos and whether it has push constants
struct ReflectedShaderInfo
{
    cc::alloc_vector<ReflectedDescriptorInfo> descriptor_infos;
    bool has_push_constants = false;
};

// Single SPIR-V shader stage bytecode patched to be compatible with PHI Vulkan conventions
struct PatchedShaderStage
{
    std::byte* data;
    size_t size;
    shader_stage stage;
    char entrypoint_name[256];
};

// patch SPIR-V bytecode to shift CBV sets upward
[[nodiscard]] PatchedShaderStage createPatchedShader(std::byte const* bytecode, size_t bytecode_size, ReflectedShaderInfo& out_info, cc::allocator* scratch_alloc);

// free the result of createPatchedShader
void freePatchedShader(PatchedShaderStage const& val);

// merge descriptor infos per entrypoint into a sorted, deduplicated list, with visiblity flags OR-d together per descriptor
cc::alloc_vector<ReflectedDescriptorInfo> mergeReflectedDescriptors(cc::span<ReflectedDescriptorInfo> inOutDescriptorInfos, cc::allocator* alloc);

// insert dummy descriptors into arguments where descriptors are missing in reflection
// NOTE: do not use, this problem is completely ill defined
size_t addDummyDescriptors(cc::span<arg::shader_arg_shape const> argShapes, cc::alloc_vector<ReflectedDescriptorInfo>& inOutDescriptors);

// issues warnings if the reflection data is incosistent with argument shapes
bool warnIfReflectionIsInconsistent(cc::span<ReflectedDescriptorInfo const> reflected_descriptors, arg::shader_arg_shapes arg_shapes);

// logs info about the reflected descriptors (to PHI_LOG)
void logReflectedDescriptors(cc::span<ReflectedDescriptorInfo const> info);

} // namespace phi::vk::util
