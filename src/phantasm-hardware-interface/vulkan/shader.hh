#pragma once

#include <cstddef>

#include <phantasm-hardware-interface/types.hh>

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

void initialize_shader(shader& s, VkDevice device, std::byte const* data, size_t size, const char* entrypoint, shader_stage stage);

[[nodiscard]] VkPipelineShaderStageCreateInfo get_shader_create_info(shader const& shader);

}
