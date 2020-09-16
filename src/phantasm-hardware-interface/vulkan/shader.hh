#pragma once

#include <cstddef>

#include <clean-core/alloc_vector.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>

#include "loader/volk.hh"

namespace phi::vk
{
struct shader
{
    VkShaderModule module;
    char const* entrypoint;
    shader_stage stage;

    void free(VkDevice device) { vkDestroyShaderModule(device, module, nullptr); }
};

struct patched_shader_intermediates
{
    cc::alloc_vector<util::patched_spirv_stage> patched_spirv;
    cc::alloc_vector<shader> shader_modules;
    cc::alloc_vector<VkPipelineShaderStageCreateInfo> shader_create_infos;

    bool has_root_constants = false;
    cc::alloc_vector<util::spirv_desc_info> sorted_merged_descriptor_infos;

    void initialize_from_libraries(VkDevice device, cc::span<phi::arg::raytracing_shader_library const> libraries, cc::allocator* alloc);

    void free(VkDevice device);
};

void initialize_shader(shader& s, VkDevice device, std::byte const* data, size_t size, const char* entrypoint, shader_stage stage);

[[nodiscard]] VkPipelineShaderStageCreateInfo get_shader_create_info(shader const& shader);

}
