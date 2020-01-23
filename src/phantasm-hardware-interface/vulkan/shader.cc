#include "shader.hh"

#include <fstream>

#include <clean-core/bit_cast.hh>

#include <phantasm-hardware-interface/detail/unique_buffer.hh>

#include "common/native_enum.hh"
#include "common/verify.hh"

VkPipelineShaderStageCreateInfo phi::vk::get_shader_create_info(const phi::vk::shader& shader)
{
    VkPipelineShaderStageCreateInfo res = {};
    res.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    res.stage = util::to_shader_stage_flags(shader.domain);
    res.module = shader.module;
    res.pName = shader.entrypoint;
    return res;
}

void phi::vk::initialize_shader(phi::vk::shader& s, VkDevice device, const std::byte* data, size_t size, const char* entrypoint, phi::shader_domain domain)
{
    VkShaderModuleCreateInfo shader_info = {};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = size;
    shader_info.pCode = cc::bit_cast<uint32_t const*>(data);

    s.domain = domain;
    s.entrypoint = entrypoint;
    PHI_VK_VERIFY_SUCCESS(vkCreateShaderModule(device, &shader_info, nullptr, &s.module));
}
