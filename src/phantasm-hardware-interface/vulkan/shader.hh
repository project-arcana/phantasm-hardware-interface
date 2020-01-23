#pragma once

#include <cstddef>

#include <phantasm-hardware-interface/types.hh>

#include "loader/volk.hh"

namespace phi::vk
{
struct shader
{
    shader_domain domain;
    VkShaderModule module;
    char const* entrypoint;
    void free(VkDevice device) { vkDestroyShaderModule(device, module, nullptr); }
};

void initialize_shader(shader& s, VkDevice device, std::byte const* data, size_t size, const char* entrypoint, shader_domain domain);

[[nodiscard]] VkPipelineShaderStageCreateInfo get_shader_create_info(shader const& shader);

}
