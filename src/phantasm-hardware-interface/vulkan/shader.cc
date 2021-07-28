#include "shader.hh"

#include <fstream>

#include <clean-core/bit_cast.hh>

#include <phantasm-hardware-interface/common/container/unique_buffer.hh>

#include "common/native_enum.hh"
#include "common/verify.hh"

VkPipelineShaderStageCreateInfo phi::vk::get_shader_create_info(const phi::vk::shader& shader)
{
    VkPipelineShaderStageCreateInfo res = {};
    res.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    res.stage = util::to_shader_stage_flags(shader.stage);
    res.module = shader.module;
    res.pName = shader.entrypoint;
    return res;
}

void phi::vk::initialize_shader(phi::vk::shader& s, VkDevice device, const std::byte* data, size_t size, const char* entrypoint, phi::shader_stage stage)
{
    VkShaderModuleCreateInfo shader_info = {};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = size;
    shader_info.pCode = reinterpret_cast<uint32_t const*>(data);

    s.stage = stage;
    s.entrypoint = entrypoint;
    PHI_VK_VERIFY_SUCCESS(vkCreateShaderModule(device, &shader_info, nullptr, &s.module));
}

void phi::vk::patched_shader_intermediates::initialize_from_libraries(VkDevice device, cc::span<const arg::raytracing_shader_library> libraries, cc::allocator* alloc)
{
    patched_spirv.reset_reserve(alloc, libraries.size());
    shader_modules.reset_reserve(alloc, libraries.size() * 16);
    shader_create_infos.reset_reserve(alloc, libraries.size() * 16);

    util::ReflectedShaderInfo spirv_info;

    for (auto const& lib : libraries)
    {
        // patch SPIR-V
        patched_spirv.push_back(util::createPatchedShader(lib.binary.data, lib.binary.size, spirv_info, alloc));
        auto const& patched_lib = patched_spirv.back();

        // create a shader per export
        for (auto const& exp : lib.shader_exports)
        {
            shader& new_shader = shader_modules.emplace_back();
            initialize_shader(new_shader, device, patched_lib.data, patched_lib.size, exp.entrypoint, exp.stage);
            shader_create_infos.push_back(get_shader_create_info(new_shader));
        }
    }

    sorted_merged_descriptor_infos = util::mergeReflectedDescriptors(spirv_info.descriptor_infos, alloc);
    has_root_constants = spirv_info.has_push_constants;
}

void phi::vk::patched_shader_intermediates::free(VkDevice device)
{
    patched_spirv = {};
    shader_modules = {};
    shader_create_infos = {};

    for (auto& module : shader_modules)
        module.free(device);

    for (auto const& ps : patched_spirv)
        util::freePatchedShader(ps);
}
