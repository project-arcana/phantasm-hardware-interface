#pragma once

#include <cstddef>

#include <clean-core/capped_vector.hh>
#include <clean-core/string.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/limits.hh>
#include <phantasm-hardware-interface/types.hh>

#include "volk.hh"

namespace phi::vk::spv
{
// We will configure DXC to shift registers (per space) in the following way to match our bindings:
// CBVs (b): 0          - Starts first
// SRVs (t): 1000       - Shifted by 1k
// UAVs (u): 2000       - Shifted by 2k
// Samplers (s): 3000   - Shifted by 3k

// Additionally, in order to be able to create and update VkDescriptorSets independently
// for handle::shader_view and the single one required for the CBV, we shift CBVs up in their _set_
// So shader arguments map as follows to sets:
// Arg 0        SRV, UAV, Sampler: 0,   CBV: 4
// Arg 1        SRV, UAV, Sampler: 1,   CBV: 5
// Arg 2        SRV, UAV, Sampler: 2,   CBV: 6
// Arg 3        SRV, UAV, Sampler: 3,   CBV: 7
// (this is required as there are no "root descriptors" in vulkan, we do
// it using spirv-reflect, see loader/spirv_patch_util for details)

inline constexpr auto cbv_binding_start = 0u;
inline constexpr auto srv_binding_start = 1000u;
inline constexpr auto uav_binding_start = 2000u;
inline constexpr auto sampler_binding_start = 3000u;

}

namespace phi::vk::util
{
struct spirv_desc_info
{
    unsigned set;
    unsigned binding;
    unsigned binding_array_size;
    VkDescriptorType type;
    VkShaderStageFlagBits visible_stage;
    VkPipelineStageFlags visible_pipeline_stage;

    constexpr bool operator==(spirv_desc_info const& rhs) const noexcept
    {
        return set == rhs.set && binding == rhs.binding && binding_array_size == rhs.binding_array_size && type == rhs.type && visible_stage == rhs.visible_stage;
    }
};

struct spirv_refl_info
{
    cc::vector<spirv_desc_info> descriptor_infos;
    bool has_push_constants = false;
};

struct patched_spirv_stage
{
    std::byte* data;
    size_t size;
    shader_domain domain;
    cc::string entrypoint_name;
};

/// we have to shift all CBVs up by [max num shader args] sets to make our API work in vulkan
/// unlike the register-to-binding shift with -fvk-[x]-shift, this cannot be done with DXC flags
/// instead we provide these helpers which use the spirv-reflect library to do the same
[[nodiscard]] patched_spirv_stage create_patched_spirv(std::byte const* bytecode, size_t bytecode_size, spirv_refl_info& out_info);

void free_patched_spirv(patched_spirv_stage const& val);

/// create a sorted, deduplicated vector of descriptor range infos from an unsorted raw output from previous patches
cc::vector<spirv_desc_info> merge_spirv_descriptors(cc::span<spirv_desc_info> desc_infos);

void print_spirv_info(cc::span<spirv_desc_info const> info);

/// returns true if the reflected descriptors are consistent with the passed arguments
/// currently only checks if the amounts are equal
[[nodiscard]] bool is_consistent_with_reflection(cc::span<spirv_desc_info const> spirv_ranges, arg::shader_arg_shapes arg_shapes);

}
