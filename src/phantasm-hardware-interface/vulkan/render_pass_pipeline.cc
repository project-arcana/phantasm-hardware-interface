#include "render_pass_pipeline.hh"

#include <clean-core/array.hh>

#include <phantasm-hardware-interface/detail/log.hh>
#include <phantasm-hardware-interface/limits.hh>

#include "common/native_enum.hh"
#include "common/verify.hh"
#include "common/vk_format.hh"
#include "loader/spirv_patch_util.hh"
#include "resources/transition_barrier.hh"
#include "shader.hh"

VkRenderPass phi::vk::create_render_pass(VkDevice device, arg::framebuffer_config const& framebuffer, const phi::pipeline_config& config)
{
    auto const sample_bits = util::to_native_sample_flags(static_cast<unsigned>(config.samples));

    VkRenderPass render_pass;
    {
        cc::capped_vector<VkAttachmentDescription, limits::max_render_targets + 1> attachments;
        cc::capped_vector<VkAttachmentReference, limits::max_render_targets> color_attachment_refs;
        VkAttachmentReference depth_attachment_ref = {};
        bool depth_present = false;

        for (auto const& rt : framebuffer.render_targets)
        {
            auto& desc = attachments.emplace_back();
            desc = {};
            desc.format = util::to_vk_format(rt.fmt);
            desc.samples = sample_bits;
            desc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            desc.initialLayout = util::to_image_layout(resource_state::render_target);
            desc.finalLayout = util::to_image_layout(resource_state::render_target);

            auto& ref = color_attachment_refs.emplace_back();
            ref.attachment = unsigned(color_attachment_refs.size() - 1);
            ref.layout = util::to_image_layout(resource_state::render_target);
        }

        if (framebuffer.depth_target != format::none)
        {
            auto& desc = attachments.emplace_back();
            desc = {};
            desc.format = util::to_vk_format(framebuffer.depth_target);
            desc.samples = sample_bits;
            desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            desc.initialLayout = util::to_image_layout(resource_state::depth_write);
            desc.finalLayout = util::to_image_layout(resource_state::depth_write);

            depth_attachment_ref.attachment = unsigned(color_attachment_refs.size());
            depth_attachment_ref.layout = util::to_image_layout(resource_state::depth_write);

            depth_present = true;
        }

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = unsigned(color_attachment_refs.size());
        subpass.pColorAttachments = color_attachment_refs.data();
        if (depth_present)
            subpass.pDepthStencilAttachment = &depth_attachment_ref;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = util::to_access_flags(resource_state::render_target);
        dependency.dstAccessMask = util::to_access_flags(resource_state::render_target);

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = unsigned(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        PHI_VK_VERIFY_SUCCESS(vkCreateRenderPass(device, &renderPassInfo, nullptr, &render_pass));
    }

    return render_pass;
}

VkRenderPass phi::vk::create_render_pass(VkDevice device, const phi::cmd::begin_render_pass& begin_rp, unsigned num_samples, cc::span<const format> override_rt_formats)
{
    CC_ASSERT(begin_rp.render_targets.size() == override_rt_formats.size() && "PSO used with wrong amount of render targets");
    auto const sample_bits = util::to_native_sample_flags(num_samples);

    cc::capped_vector<VkAttachmentDescription, limits::max_render_targets + 1> attachments;
    cc::capped_vector<VkAttachmentReference, limits::max_render_targets> color_attachment_refs;
    VkAttachmentReference depth_attachment_ref = {};
    bool depth_present = false;

    for (uint8_t i = 0u; i < begin_rp.render_targets.size(); ++i)
    {
        auto const& rt = begin_rp.render_targets[i];

        auto& desc = attachments.emplace_back();
        desc = {};
        desc.format = util::to_vk_format(override_rt_formats[i]);
        desc.samples = sample_bits;
        desc.loadOp = util::to_native(rt.clear_type);
        desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // by default, render passes always store
        desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        desc.initialLayout = util::to_image_layout(resource_state::render_target);
        desc.finalLayout = util::to_image_layout(resource_state::render_target);

        auto& ref = color_attachment_refs.emplace_back();
        ref.attachment = unsigned(color_attachment_refs.size() - 1);
        ref.layout = util::to_image_layout(resource_state::render_target);
    }

    if (begin_rp.depth_target.rv.resource != handle::null_resource)
    {
        auto const& ds = begin_rp.depth_target;
        auto& desc = attachments.emplace_back();
        desc = {};
        desc.format = util::to_vk_format(ds.rv.pixel_format);
        desc.samples = sample_bits;
        desc.loadOp = util::to_native(ds.clear_type);
        desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.stencilLoadOp = util::to_native(ds.clear_type);
        desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.initialLayout = util::to_image_layout(resource_state::depth_write);
        desc.finalLayout = util::to_image_layout(resource_state::depth_write);

        depth_attachment_ref.attachment = unsigned(color_attachment_refs.size());
        depth_attachment_ref.layout = util::to_image_layout(resource_state::depth_write);

        depth_present = true;
    }

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = unsigned(color_attachment_refs.size());
    subpass.pColorAttachments = color_attachment_refs.data();
    if (depth_present)
        subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = util::to_access_flags(resource_state::render_target);
    dependency.dstAccessMask = util::to_access_flags(resource_state::render_target);

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = unsigned(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkRenderPass res_rp;
    PHI_VK_VERIFY_SUCCESS(vkCreateRenderPass(device, &renderPassInfo, nullptr, &res_rp));
    return res_rp;
}

VkPipeline phi::vk::create_pipeline(VkDevice device,
                                    VkRenderPass render_pass,
                                    VkPipelineLayout pipeline_layout,
                                    cc::span<const util::patched_spirv_stage> shaders,
                                    const phi::pipeline_config& config,
                                    cc::span<const VkVertexInputAttributeDescription> vertex_attribs,
                                    uint32_t vertex_size,
                                    arg::framebuffer_config const& framebuf_config)
{
    bool const no_vertices = vertex_size == 0;
    CC_ASSERT(no_vertices ? vertex_attribs.empty() : true && "Did not expect vertex attributes for no-vertex mode");

    cc::capped_vector<shader, 6> shader_stages;
    cc::capped_vector<VkPipelineShaderStageCreateInfo, 6> shader_stage_create_infos;

    bool has_pixel_shader = false;
    for (auto const& shader : shaders)
    {
        auto& new_shader = shader_stages.emplace_back();
        initialize_shader(new_shader, device, shader.data, shader.size, shader.entrypoint_name.c_str(), shader.stage);

        shader_stage_create_infos.push_back(get_shader_create_info(new_shader));

        if (shader.stage == shader_stage::pixel)
            has_pixel_shader = true;
    }

    CC_ASSERT(framebuf_config.render_targets.empty() ? true : has_pixel_shader && "creating a PSO with rendertargets, but missing pixel shader");

    VkVertexInputBindingDescription vertex_bind_desc = {};
    vertex_bind_desc.binding = 0;
    vertex_bind_desc.stride = vertex_size;
    vertex_bind_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = no_vertices ? 0 : 1;
    vertex_input_info.pVertexBindingDescriptions = no_vertices ? nullptr : &vertex_bind_desc;
    vertex_input_info.vertexAttributeDescriptionCount = no_vertices ? 0 : unsigned(vertex_attribs.size());
    vertex_input_info.pVertexAttributeDescriptions = no_vertices ? nullptr : vertex_attribs.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = util::to_native(config.topology);
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // we use dynamic viewports and scissors, these initial values are irrelevant
    auto const initial_size = VkExtent2D{10, 10};

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = float(initial_size.width);
    viewport.height = float(initial_size.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = initial_size;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = util::to_native(config.cull);
    rasterizer.frontFace = config.frontface_counterclockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f;          // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

    VkPipelineRasterizationConservativeStateCreateInfoEXT conservative_raster = {};

    if (config.conservative_raster)
    {
        conservative_raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
        conservative_raster.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
        rasterizer.pNext = &conservative_raster;
    }

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = util::to_native_sample_flags(static_cast<unsigned>(config.samples));
    multisampling.minSampleShading = 1.0f;          // Optional
    multisampling.pSampleMask = nullptr;            // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE;      // Optional

    cc::capped_vector<VkPipelineColorBlendAttachmentState, limits::max_render_targets> color_blend_attachments;

    for (auto const& rt : framebuf_config.render_targets)
    {
        VkPipelineColorBlendAttachmentState& rt_attachment = color_blend_attachments.emplace_back();
        rt_attachment = {};
        rt_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        rt_attachment.blendEnable = rt.blend_enable ? VK_TRUE : VK_FALSE;
        rt_attachment.srcColorBlendFactor = util::to_native(rt.state.blend_color_src);
        rt_attachment.dstColorBlendFactor = util::to_native(rt.state.blend_color_dest);
        rt_attachment.colorBlendOp = util::to_native(rt.state.blend_op_color);
        rt_attachment.srcAlphaBlendFactor = util::to_native(rt.state.blend_alpha_src);
        rt_attachment.dstAlphaBlendFactor = util::to_native(rt.state.blend_alpha_dest);
        rt_attachment.alphaBlendOp = util::to_native(rt.state.blend_op_alpha);
    }

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = framebuf_config.logic_op_enable ? VK_TRUE : VK_FALSE;
    colorBlending.logicOp = util::to_native(framebuf_config.logic_op);
    colorBlending.attachmentCount = static_cast<uint32_t>(color_blend_attachments.size());
    colorBlending.pAttachments = color_blend_attachments.data();
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    cc::array constexpr dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = uint32_t(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depth == phi::depth_function::none ? VK_FALSE : VK_TRUE;
    depthStencil.depthWriteEnable = config.depth_readonly ? VK_FALSE : VK_TRUE;
    depthStencil.depthCompareOp = util::to_native(config.depth);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {}; // Optional
    depthStencil.back = {};  // Optional

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = uint32_t(shader_stage_create_infos.size());
    pipelineInfo.pStages = shader_stage_create_infos.data();
    pipelineInfo.pVertexInputState = &vertex_input_info;
    pipelineInfo.pInputAssemblyState = &input_assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil; // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline_layout;
    pipelineInfo.renderPass = render_pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr; // Optional
    pipelineInfo.basePipelineIndex = -1;       // Optional

    VkPipeline res;
    PHI_VK_VERIFY_SUCCESS(vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &res));

    for (auto& shader : shader_stages)
    {
        shader.free(device);
    }

    return res;
}

VkPipeline phi::vk::create_compute_pipeline(VkDevice device, VkPipelineLayout pipeline_layout, const util::patched_spirv_stage& compute_shader)
{
    shader shader_stage;
    initialize_shader(shader_stage, device, compute_shader.data, compute_shader.size, compute_shader.entrypoint_name.c_str(), shader_stage::compute);

    VkComputePipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.stage = get_shader_create_info(shader_stage);

    VkPipeline res;
    PHI_VK_VERIFY_SUCCESS(vkCreateComputePipelines(device, nullptr, 1, &pipeline_info, nullptr, &res));
    shader_stage.free(device);
    return res;
}

VkPipeline phi::vk::create_raytracing_pipeline(VkDevice device,
                                               VkPipelineLayout pipeline_layout,
                                               cc::span<const phi::vk::util::patched_spirv_stage> shaders,
                                               phi::arg::raytracing_shader_libraries libraries,
                                               phi::arg::raytracing_argument_associations arg_assocs,
                                               phi::arg::raytracing_hit_groups hit_groups,
                                               unsigned max_recursion,
                                               unsigned max_payload_size_bytes,
                                               unsigned max_attribute_size_bytes)
{
    CC_ASSERT(libraries.size() > 0 && arg_assocs.size() <= limits::max_raytracing_argument_assocs && "zero libraries or too many argument associations");
    CC_ASSERT(hit_groups.size() <= limits::max_raytracing_hit_groups && "too many hit groups");

    cc::vector<shader> shader_modules;
    cc::vector<VkPipelineShaderStageCreateInfo> shader_create_infos;
    shader_modules.reserve(libraries.size() * 16);
    shader_create_infos.reserve(shader_modules.size());

    for (auto const& lib : libraries)
    {
        for (auto const& exp : lib.exports)
        {
            shader& new_shader = shader_modules.emplace_back();
            initialize_shader(new_shader, device, lib.binary.data, lib.binary.size, exp.entrypoint, exp.stage);
            shader_create_infos.push_back(get_shader_create_info(new_shader));
        }
    }

    auto const f_get_shader_index_by_symbol = [&](char const* symbol, shader_stage stage) -> uint32_t {
        if (!symbol)
            return VK_SHADER_UNUSED_NV;

        for (uint32_t i = 0u; i < shader_modules.size(); ++i)
        {
            if (shader_modules[i].stage != stage)
                continue;

            if (std::strcmp(shader_modules[i].entrypoint, symbol) == 0)
            {
                return i;
            }
        }
        PHI_LOG_WARN("createRaytracingPipeline: Failed to find export symbol {} in provided libraries");
        return VK_SHADER_UNUSED_NV;
    };

    cc::vector<VkRayTracingShaderGroupCreateInfoNV> group_infos;
    group_infos.reserve(hit_groups.size());

    for (auto const& hg : hit_groups)
    {
        VkRayTracingShaderGroupCreateInfoNV& new_info = group_infos.emplace_back();
        new_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
        new_info.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;
        new_info.generalShader = VK_SHADER_UNUSED_NV; // always unused in triangle hit groups
        new_info.anyHitShader = f_get_shader_index_by_symbol(hg.any_hit_name, shader_stage::ray_any_hit);
        new_info.closestHitShader = f_get_shader_index_by_symbol(hg.closest_hit_name, shader_stage::ray_closest_hit);
        new_info.intersectionShader = f_get_shader_index_by_symbol(hg.intersection_name, shader_stage::ray_intersect);
    }

    VkRayTracingPipelineCreateInfoNV pso_info = {};
    pso_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;

    pso_info.flags = 0; // TODO
    pso_info.stageCount = uint32_t(shader_create_infos.size());
    pso_info.pStages = shader_create_infos.data();
    pso_info.groupCount = uint32_t(group_infos.size());
    pso_info.pGroups = group_infos.data();
    pso_info.maxRecursionDepth = max_recursion;
    pso_info.layout = pipeline_layout;
    // not deriving
    pso_info.basePipelineHandle = VK_NULL_HANDLE;
    pso_info.basePipelineIndex = -1;

    VkPipeline res;
    PHI_VK_VERIFY_SUCCESS(vkCreateRayTracingPipelinesNV(device, nullptr, 1, &pso_info, nullptr, &res));

    for (auto& shader : shader_modules)
    {
        shader.free(device);
    }

    return res;
}
