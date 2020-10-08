#pragma once

#include <cstddef>

#include <clean-core/alloc_vector.hh>
#include <clean-core/string.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/limits.hh>
#include <phantasm-hardware-interface/types.hh>

#include "volk.hh"

namespace phi::vk::spv
{
enum spv_constants_e : unsigned
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
struct spirv_desc_info
{
    unsigned set;
    unsigned binding;
    unsigned binding_array_size;
    VkDescriptorType type;
    VkShaderStageFlagBits visible_stage;
    VkPipelineStageFlags visible_pipeline_stage;

    bool operator==(spirv_desc_info const& rhs) const noexcept
    {
        static_assert(std::has_unique_object_representations_v<spirv_desc_info>, "spirv_desc_info cannot be memcmp'd");
        return std::memcmp(this, &rhs, sizeof(*this)) == 0;
        //        return set == rhs.set && binding == rhs.binding && binding_array_size == rhs.binding_array_size && type == rhs.type && visible_stage == rhs.visible_stage;
    }
};

struct spirv_refl_info
{
    cc::alloc_vector<spirv_desc_info> descriptor_infos;
    bool has_push_constants = false;
};

struct patched_spirv_stage
{
    std::byte* data;
    size_t size;
    shader_stage stage;
    cc::string entrypoint_name;
};

/// we have to shift all CBVs up by [max num shader args] sets to make our API work in vulkan
/// unlike the register-to-binding shift with -fvk-[x]-shift, this cannot be done with DXC flags
/// instead we provide these helpers which use the spirv-reflect library to do the same
[[nodiscard]] patched_spirv_stage create_patched_spirv(std::byte const* bytecode, size_t bytecode_size, spirv_refl_info& out_info, cc::allocator* scratch_alloc);

void free_patched_spirv(patched_spirv_stage const& val);

/// create a sorted, deduplicated vector of descriptor range infos from an unsorted raw output from previous patches
cc::alloc_vector<spirv_desc_info> merge_spirv_descriptors(cc::span<spirv_desc_info> desc_infos, cc::allocator* alloc);

void print_spirv_info(cc::span<spirv_desc_info const> info);

/// returns true if the reflected descriptors are consistent with the passed arguments
/// currently only checks if the amounts are equal
[[nodiscard]] bool is_consistent_with_reflection(cc::span<spirv_desc_info const> spirv_ranges, arg::shader_arg_shapes arg_shapes);

//
// serialization of fully processed SPIR-V

bool write_patched_spirv(patched_spirv_stage const& spirv, cc::span<spirv_desc_info const> merged_descriptor_info, bool has_root_consts, char const* out_path);

struct patched_spirv_data_nonowning
{
    std::byte const* binary_data;
    size_t binary_size_bytes;
    char const* entrypoint_name;
    cc::span<spirv_desc_info const> descriptor_infos;
    shader_stage stage;
    bool has_root_constants;
};

bool parse_patched_spirv(cc::span<std::byte const> data, patched_spirv_data_nonowning& out_parsed);

}
