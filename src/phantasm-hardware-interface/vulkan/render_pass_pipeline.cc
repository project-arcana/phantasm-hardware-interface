#include "render_pass_pipeline.hh"

#include <clean-core/array.hh>

#include <phantasm-hardware-interface/limits.hh>

#include "common/native_enum.hh"
#include "common/verify.hh"
#include "common/vk_format.hh"
#include "loader/spirv_patch_util.hh"
#include "resources/transition_barrier.hh"
#include "shader.hh"

VkRenderPass phi::vk::create_render_pass(VkDevice device, arg::framebuffer_config const& framebuffer, const phi::graphics_pipeline_config& config)
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
            desc.format = util::to_vk_format(rt.format);
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

        for (format ds : framebuffer.depth_target)
        {
            auto& desc = attachments.emplace_back();
            desc = {};
            desc.format = util::to_vk_format(ds);
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

    if (begin_rp.depth_target.sve.resource != handle::null_resource)
    {
        auto const& ds = begin_rp.depth_target;
        auto& desc = attachments.emplace_back();
        desc = {};
        desc.format = util::to_vk_format(ds.sve.pixel_format);
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
                                    const phi::graphics_pipeline_config& config,
                                    cc::span<const VkVertexInputAttributeDescription> vertex_attribs,
                                    uint32_t vertex_size,
                                    arg::framebuffer_config const& framebuf_config)
{
    bool const no_vertices = vertex_size == 0;
    if (no_vertices)
    {
        CC_ASSERT(vertex_attribs.empty() && "Did not expect vertex attributes for no-vertex mode");
    }

    cc::capped_vector<shader, 6> shader_stages;
    cc::capped_vector<VkPipelineShaderStageCreateInfo, 6> shader_stage_create_infos;

    for (auto const& shader : shaders)
    {
        auto& new_shader = shader_stages.emplace_back();
        initialize_shader(new_shader, device, shader.data, shader.size, shader.entrypoint_name.c_str(), shader.stage);

        shader_stage_create_infos.push_back(get_shader_create_info(new_shader));
    }

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
    input_assembly.topology = no_vertices ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : util::to_native(config.topology);
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
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = no_vertices ? VK_CULL_MODE_FRONT_BIT : util::to_native(config.cull);
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f;          // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

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
        rt_attachment.srcColorBlendFactor = util::to_native(rt.blend_color_src);
        rt_attachment.dstColorBlendFactor = util::to_native(rt.blend_color_dest);
        rt_attachment.colorBlendOp = util::to_native(rt.blend_op_color);
        rt_attachment.srcAlphaBlendFactor = util::to_native(rt.blend_alpha_src);
        rt_attachment.dstAlphaBlendFactor = util::to_native(rt.blend_alpha_dest);
        rt_attachment.alphaBlendOp = util::to_native(rt.blend_op_alpha);
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
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
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
    vkCreateComputePipelines(device, nullptr, 1, &pipeline_info, nullptr, &res);
    shader_stage.free(device);
    return res;
}
