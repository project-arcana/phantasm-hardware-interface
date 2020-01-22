#include "shader.hh"

#include <fstream>

#include <clean-core/bit_cast.hh>

#include <phantasm-hardware-interface/detail/unique_buffer.hh>

#include "common/native_enum.hh"
#include "common/verify.hh"

namespace
{
[[nodiscard]] constexpr char const* get_default_entrypoint(pr::backend::shader_domain domain)
{
    switch (domain)
    {
    case pr::backend::shader_domain::pixel:
        return "main_ps";
    case pr::backend::shader_domain::vertex:
        return "main_vs";
    case pr::backend::shader_domain::domain:
        return "main_ds";
    case pr::backend::shader_domain::hull:
        return "main_hs";
    case pr::backend::shader_domain::geometry:
        return "main_gs";

    case pr::backend::shader_domain::compute:
        return "main_cs";

    default:
        return "main";
    }
}
}

VkPipelineShaderStageCreateInfo pr::backend::vk::get_shader_create_info(const pr::backend::vk::shader& shader)
{
    VkPipelineShaderStageCreateInfo res = {};
    res.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    res.stage = util::to_shader_stage_flags(shader.domain);
    res.module = shader.module;
    res.pName = shader.entrypoint;
    return res;
}

void pr::backend::vk::initialize_shader(pr::backend::vk::shader& s, VkDevice device, const std::byte* data, size_t size, pr::backend::shader_domain domain)
{
    VkShaderModuleCreateInfo shader_info = {};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = size;
    shader_info.pCode = cc::bit_cast<uint32_t const*>(data);

    s.domain = domain;
    s.entrypoint = get_default_entrypoint(domain);
    PR_VK_VERIFY_SUCCESS(vkCreateShaderModule(device, &shader_info, nullptr, &s.module));
}
