#pragma once

#include <clean-core/array.hh>
#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/limits.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk
{
namespace detail
{
// 1 pipeline layout - n descriptor set layouts
// 1 descriptor set layouts - n bindings
// spaces: descriptor sets
// registers: bindings
struct pipeline_layout_params
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

    /// lists bindings (descriptors) for a single set
    struct descriptor_set_params
    {
        cc::capped_vector<VkDescriptorSetLayoutBinding, 64> bindings;

        void add_descriptor(VkDescriptorType type, unsigned binding, unsigned array_size, VkShaderStageFlags visibility);

        // this function is no longer in use
        [[deprecated("dropped support for immutable samplers")]] void fill_in_immutable_samplers(cc::span<VkSampler const> samplers);

        VkDescriptorSetLayout create_layout(VkDevice device) const;
    };

    /// bindings per set (2 * args - doubled for CBVs)
    cc::capped_vector<descriptor_set_params, limits::max_shader_arguments * 2> descriptor_sets;

    /// merged visibilities per set
    cc::capped_vector<VkPipelineStageFlags, limits::max_shader_arguments * 2> merged_pipeline_visibilities;

    void initialize_from_reflection_info(cc::span<util::spirv_desc_info const> reflection_info);
};
}

struct pipeline_layout
{
    /// The descriptor set layouts, two per shader argument:
    /// One for samplers, SRVs and UAVs, one for CBVs, shifted behind the first types
    cc::capped_vector<VkDescriptorSetLayout, limits::max_shader_arguments * 2> descriptor_set_layouts;

    /// The pipeline stages (only shader stages) which have access to
    /// the respective descriptor sets (parallel array)
    cc::capped_vector<VkPipelineStageFlags, limits::max_shader_arguments * 2> descriptor_set_visibilities;

    /// The pipeline layout itself
    VkPipelineLayout raw_layout = nullptr;
    VkPipelineStageFlags push_constant_stages;

    void initialize(VkDevice device, cc::span<util::spirv_desc_info const> descriptor_info, bool add_push_constants);

    void free(VkDevice device);

    bool has_push_constants() const { return push_constant_stages != VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM; }
    void print() const;
};

}
