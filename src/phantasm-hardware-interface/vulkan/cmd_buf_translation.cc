#include "cmd_buf_translation.hh"

#ifdef PHI_HAS_OPTICK
#include <optick/optick.h>
#endif

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/command_reading.hh>
#include <phantasm-hardware-interface/common/format_size.hh>
#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/common/sse_hash.hh>
#include <phantasm-hardware-interface/util.hh>

#include "common/native_enum.hh"
#include "common/util.hh"
#include "common/verify.hh"
#include "pools/accel_struct_pool.hh"
#include "pools/cmd_list_pool.hh"
#include "pools/pipeline_layout_cache.hh"
#include "pools/pipeline_pool.hh"
#include "pools/query_pool.hh"
#include "pools/resource_pool.hh"
#include "pools/shader_view_pool.hh"
#include "resources/transition_barrier.hh"

void phi::vk::command_list_translator::translateCommandList(
    VkCommandBuffer list, handle::command_list list_handle, vk_incomplete_state_cache* state_cache, std::byte const* buffer, size_t buffer_size)
{
    _cmd_list = list;
    _cmd_list_handle = list_handle;
    _state_cache = state_cache;

    _bound.reset();
    _state_cache->reset();
    _last_code_location.reset();

    {
        // start Optick context
#ifdef PHI_HAS_OPTICK
        OPTICK_GPU_CONTEXT(_cmd_list);
        _current_optick_event = nullptr;
        OPTICK_GPU_EVENT("PHI Command List");
#endif

        // translate all contained commands
        command_stream_parser parser(buffer, buffer_size);
        for (auto const& cmd : parser)
        {
            cmd::detail::dynamic_dispatch(cmd, *this);
        }

        // close pending render pass
        if (_bound.raw_render_pass != nullptr)
        {
            // end the last render pass
            vkCmdEndRenderPass(_cmd_list);
        }

        // end last pending optick event
#ifdef PHI_HAS_OPTICK
        if (_current_optick_event)
        {
            Optick::GPUEvent::Stop(*_current_optick_event);
            _current_optick_event = nullptr;
        }
#endif
    }

    // close the list
    PHI_VK_VERIFY_SUCCESS(vkEndCommandBuffer(_cmd_list));

    // done
}

void phi::vk::command_list_translator::execute(const phi::cmd::begin_render_pass& begin_rp)
{
    CC_ASSERT(_bound.raw_render_pass == nullptr && "double cmd::begin_render_pass - missing cmd::end_render_pass?");
    CC_ASSERT(begin_rp.viewport.width + begin_rp.viewport.height != 0 && "recording begin_render_pass with empty viewport");

    // the image views used in this framebuffer
    cc::capped_vector<VkImageView, limits::max_render_targets + 1> fb_image_views;
    // the image views used in this framebuffer, EXCLUDING possible backbuffer views
    // these are the ones which will get deleted alongside this framebuffer
    cc::capped_vector<VkImageView, limits::max_render_targets + 1> fb_image_views_to_clean_up;
    // clear values for the render targets and depth target
    cc::capped_vector<VkClearValue, limits::max_render_targets + 1> clear_values;
    // formats of the render targets
    cc::capped_vector<format, limits::max_render_targets> formats_flat;

    // inferred info
    int num_fb_samples = 1;
    tg::isize2 fb_size = begin_rp.viewport;

    // extract all information that is required in flat arrays for Vk structs
    for (auto const& rt : begin_rp.render_targets)
    {
        // rt format
        formats_flat.push_back(rt.rv.texture_info.pixel_format);

        // image view
        if (_globals.pool_resources->isBackbuffer(rt.rv.resource))
        {
            fb_image_views.push_back(_globals.pool_resources->getBackbufferView(rt.rv.resource));
        }
        else
        {
            fb_image_views.push_back(_globals.pool_shader_views->makeImageView(rt.rv, false, false));
            fb_image_views_to_clean_up.push_back(fb_image_views.back());
        }

        // clear val
        auto& cv = clear_values.emplace_back();
        std::memcpy(&cv.color.float32, &rt.clear_value, sizeof(rt.clear_value));
    }

    if (begin_rp.depth_target.rv.resource.is_valid())
    {
        // image view
        fb_image_views.push_back(_globals.pool_shader_views->makeImageView(begin_rp.depth_target.rv, false, false));
        fb_image_views_to_clean_up.push_back(fb_image_views.back());

        // clear val
        auto& cv = clear_values.emplace_back();
        cv.depthStencil = {begin_rp.depth_target.clear_value_depth, uint32_t(begin_rp.depth_target.clear_value_stencil)};
    }

    // infer amount of samples and effective render target sizes from the command
    {
        phi::resource_view const* rv = nullptr;

        if (!begin_rp.render_targets.empty())
        {
            rv = &begin_rp.render_targets[0].rv;
        }
        else if (begin_rp.depth_target.rv.resource.is_valid())
        {
            rv = &begin_rp.depth_target.rv;
        }

        if (rv != nullptr)
        {
            auto const& img_info = _globals.pool_resources->getTextureDescription(rv->resource);
            num_fb_samples = img_info.num_samples;
            fb_size = phi::util::get_mip_size({img_info.width, img_info.height}, rv->texture_info.mip_start);
        }
    }

    // create or retrieve a render pass from cache matching the configuration
    auto const render_pass = _globals.pool_pipeline_states->getOrCreateRenderPass(begin_rp, num_fb_samples, formats_flat);

    // a render pass always changes
    //      - The framebuffer
    //      - The vkCmdBeginRenderPass/vkCmdEndRenderPass state
    _bound.raw_render_pass = render_pass;

    // create a new framebuffer on the fly
    {
        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass;
        fb_info.attachmentCount = uint32_t(fb_image_views.size());
        fb_info.pAttachments = fb_image_views.data();
        fb_info.width = uint32_t(fb_size.width);
        fb_info.height = uint32_t(fb_size.height);
        fb_info.layers = 1;

        // Create the framebuffer
        PHI_VK_VERIFY_SUCCESS(vkCreateFramebuffer(_globals.device, &fb_info, nullptr, &_bound.raw_framebuffer));

        // Associate the framebuffer and all created image views with the current command list so they will get cleaned up
        _globals.pool_cmd_lists->addAssociatedFramebuffer(_cmd_list_handle, _bound.raw_framebuffer, fb_image_views_to_clean_up);
    }

    // begin a new render pass
    {
        VkRenderPassBeginInfo rp_begin_info = {};
        rp_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin_info.renderPass = render_pass;
        rp_begin_info.framebuffer = _bound.raw_framebuffer;
        rp_begin_info.renderArea.offset = {0, 0};
        rp_begin_info.renderArea.extent.width = uint32_t(fb_size.width);
        rp_begin_info.renderArea.extent.height = uint32_t(fb_size.height);
        rp_begin_info.clearValueCount = uint32_t(clear_values.size());
        rp_begin_info.pClearValues = clear_values.data();

        // NOTE: the viewport situation is as follows on vulkan
        // outermost: VkFramebuffer
        //          size
        // next: VkRenderPassBeginInfo::renderArea
        //          size + offset from topleft, must be within or equal to VkFramebuffer size
        // finally: vkCmdSetViewport and vkCmdSetScissor
        //          size + offset           offset
        //
        // the cleared area depends upon VkRenderPassBeginInfo::renderArea, thus we set that and consequently the VkFramebuffer size
        // to the size of the first render target instead of the specified viewport
        // this behavior is consistent with d3d12

        // NOTE: vulkans viewport has a flipped y axis
        // we set a normal, non flipped viewport here and flip via the -fvk-invert-y flag in dxc

        VkViewport viewport = {};
        viewport.x = float(begin_rp.viewport_offset.x);
        viewport.y = float(begin_rp.viewport_offset.y);
        viewport.width = float(begin_rp.viewport.width);
        viewport.height = float(begin_rp.viewport.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent.width = unsigned(begin_rp.viewport.width + begin_rp.viewport_offset.x);
        scissor.extent.height = unsigned(begin_rp.viewport.height + begin_rp.viewport_offset.y);

        vkCmdSetViewport(_cmd_list, 0, 1, &viewport);
        vkCmdSetScissor(_cmd_list, 0, 1, &scissor);

        vkCmdBeginRenderPass(_cmd_list, &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    }
}

void phi::vk::command_list_translator::execute(const phi::cmd::draw& draw)
{
    if (_bound.update_pso(draw.pipeline_state))
    {
        // a new handle::pipeline_state invalidates (!= always changes)
        //      - The bound pipeline layout
        //      - The bound pipeline
        auto const& pso_node = _globals.pool_pipeline_states->get(draw.pipeline_state);
        _bound.update_pipeline_layout(pso_node.associated_pipeline_layout->raw_layout);
        vkCmdBindPipeline(_cmd_list, VK_PIPELINE_BIND_POINT_GRAPHICS, pso_node.raw_pipeline);
    }

    // Index buffer (optional)
    if (draw.index_buffer != _bound.index_buffer)
    {
        _bound.index_buffer = draw.index_buffer;
        if (draw.index_buffer.is_valid())
        {
            auto const& ind_buf_info = _globals.pool_resources->getBufferInfo(draw.index_buffer);
            vkCmdBindIndexBuffer(_cmd_list, ind_buf_info.raw_buffer, 0, (ind_buf_info.stride == 4) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
        }
    }

    // Vertex buffers
    bind_vertex_buffers(draw.vertex_buffers);

    // Shader arguments
    bind_shader_arguments(draw.pipeline_state, draw.root_constants, draw.shader_arguments, VK_PIPELINE_BIND_POINT_GRAPHICS);

    // Scissor
    if (draw.scissor.min.x != -1)
    {
        VkRect2D scissor_rect;
        scissor_rect.offset = VkOffset2D{draw.scissor.min.x, draw.scissor.min.y};
        scissor_rect.extent = VkExtent2D{uint32_t(draw.scissor.max.x - draw.scissor.min.x), uint32_t(draw.scissor.max.y - draw.scissor.min.y)};
        vkCmdSetScissor(_cmd_list, 0, 1, &scissor_rect);
    }

    // Draw command
    if (draw.index_buffer.is_valid())
    {
        vkCmdDrawIndexed(_cmd_list, draw.num_indices, draw.num_instances, draw.index_offset, draw.vertex_offset, 0);
    }
    else
    {
        vkCmdDraw(_cmd_list, draw.num_indices, draw.num_instances, draw.index_offset, 0);
    }
}

void phi::vk::command_list_translator::execute(const phi::cmd::draw_indirect& draw_indirect)
{
    if (_bound.update_pso(draw_indirect.pipeline_state))
    {
        // a new handle::pipeline_state invalidates (!= always changes)
        //      - The bound pipeline layout
        //      - The bound pipeline
        auto const& pso_node = _globals.pool_pipeline_states->get(draw_indirect.pipeline_state);
        _bound.update_pipeline_layout(pso_node.associated_pipeline_layout->raw_layout);
        vkCmdBindPipeline(_cmd_list, VK_PIPELINE_BIND_POINT_GRAPHICS, pso_node.raw_pipeline);
    }

    // Index buffer (optional)
    if (draw_indirect.index_buffer != _bound.index_buffer)
    {
        _bound.index_buffer = draw_indirect.index_buffer;
        if (draw_indirect.index_buffer.is_valid())
        {
            auto const& ind_buf_info = _globals.pool_resources->getBufferInfo(draw_indirect.index_buffer);
            vkCmdBindIndexBuffer(_cmd_list, ind_buf_info.raw_buffer, 0, (ind_buf_info.stride == 4) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
        }
    }

    // Vertex buffer
    bind_vertex_buffers(draw_indirect.vertex_buffers);

    // Shader arguments
    bind_shader_arguments(draw_indirect.pipeline_state, draw_indirect.root_constants, draw_indirect.shader_arguments, VK_PIPELINE_BIND_POINT_GRAPHICS);

    // Indirect draw command

    auto const gpu_command_size_bytes = draw_indirect.index_buffer.is_valid() ? sizeof(gpu_indirect_command_draw_indexed) : sizeof(gpu_indirect_command_draw);
    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(draw_indirect.indirect_argument_buffer, draw_indirect.argument_buffer_offset_bytes,
                                                              draw_indirect.num_arguments * gpu_command_size_bytes)
              && "indirect argument buffer accessed OOB on GPU");

    auto const raw_argument_buffer = _globals.pool_resources->getRawBuffer(draw_indirect.indirect_argument_buffer);
    if (draw_indirect.index_buffer.is_valid())
    {
        static_assert(sizeof(VkDrawIndexedIndirectCommand) == sizeof(gpu_indirect_command_draw_indexed), "gpu argument type compiles to incorrect "
                                                                                                         "size");
        vkCmdDrawIndexedIndirect(_cmd_list, raw_argument_buffer, VkDeviceSize(draw_indirect.argument_buffer_offset_bytes),
                                 draw_indirect.num_arguments, sizeof(VkDrawIndexedIndirectCommand));
    }
    else
    {
        static_assert(sizeof(VkDrawIndirectCommand) == sizeof(gpu_indirect_command_draw), "gpu argument type compiles to incorrect size");
        vkCmdDrawIndirect(_cmd_list, raw_argument_buffer, VkDeviceSize(draw_indirect.argument_buffer_offset_bytes), draw_indirect.num_arguments,
                          sizeof(VkDrawIndirectCommand));
    }
}

void phi::vk::command_list_translator::execute(const phi::cmd::dispatch& dispatch)
{
    auto const& pso_node = _globals.pool_pipeline_states->get(dispatch.pipeline_state);

    if (_bound.update_pso(dispatch.pipeline_state))
    {
        // a new handle::pipeline_state invalidates (!= always changes)
        //      - The bound pipeline layout
        //      - The bound pipeline

        _bound.update_pipeline_layout(pso_node.associated_pipeline_layout->raw_layout);
        vkCmdBindPipeline(_cmd_list, VK_PIPELINE_BIND_POINT_COMPUTE, pso_node.raw_pipeline);
    }

    // Shader arguments
    bind_shader_arguments(dispatch.pipeline_state, dispatch.root_constants, dispatch.shader_arguments, VK_PIPELINE_BIND_POINT_COMPUTE);

    // Dispatch command
    vkCmdDispatch(_cmd_list, dispatch.dispatch_x, dispatch.dispatch_y, dispatch.dispatch_z);
}

void phi::vk::command_list_translator::execute(const phi::cmd::dispatch_indirect& dispatch_indirect)
{
    auto const& pso_node = _globals.pool_pipeline_states->get(dispatch_indirect.pipeline_state);

    if (_bound.update_pso(dispatch_indirect.pipeline_state))
    {
        // a new handle::pipeline_state invalidates (!= always changes)
        //      - The bound pipeline layout
        //      - The bound pipeline

        _bound.update_pipeline_layout(pso_node.associated_pipeline_layout->raw_layout);
        vkCmdBindPipeline(_cmd_list, VK_PIPELINE_BIND_POINT_COMPUTE, pso_node.raw_pipeline);
    }

    // Shader arguments
    bind_shader_arguments(dispatch_indirect.pipeline_state, dispatch_indirect.root_constants, dispatch_indirect.shader_arguments, VK_PIPELINE_BIND_POINT_COMPUTE);

    constexpr auto gpu_command_size_bytes = uint32_t(sizeof(gpu_indirect_command_dispatch));
    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(dispatch_indirect.argument_buffer_addr, dispatch_indirect.num_arguments * gpu_command_size_bytes)
              && "indirect argument buffer accessed OOB on GPU");

    auto const raw_argument_buffer = _globals.pool_resources->getRawBuffer(dispatch_indirect.argument_buffer_addr);

    // Vulkan has no equivalent to D3D12 ExecuteIndirect
    // (except for VK_NVX_device_generated_commands, nvidia only)
    // that means we have to call this manually multiple times
    // counter buffer would be impossible
    auto bufferOffset = dispatch_indirect.argument_buffer_addr.offset_bytes;
    for (auto i = 0u; i < dispatch_indirect.num_arguments; ++i)
    {
        vkCmdDispatchIndirect(_cmd_list, raw_argument_buffer, bufferOffset);
        bufferOffset += gpu_command_size_bytes;
    }
}

void phi::vk::command_list_translator::execute(const phi::cmd::end_render_pass&)
{
    CC_ASSERT(_bound.raw_render_pass != nullptr && "cmd::end_render_pass while no render pass is active");

    vkCmdEndRenderPass(_cmd_list);
    _bound.raw_render_pass = nullptr;

    _bound.reset();
}

void phi::vk::command_list_translator::execute(const phi::cmd::transition_resources& transition_res)
{
    // NOTE: Barriers adhere to some special rules in the vulkan backend:
    // 1. They must not occur within an active render pass
    // 2. Render passes always expect all render targets to be transitioned to resource_state::render_target
    //    and depth targets to be transitioned to resource_state::depth_write
    CC_ASSERT(_bound.raw_render_pass == nullptr && "Vulkan resource transitions must not occur during render passes");

    barrier_bundle<limits::max_resource_transitions, limits::max_resource_transitions, limits::max_resource_transitions> barriers;

    for (auto const& transition : transition_res.transitions)
    {
        auto const after_dep = util::to_pipeline_stage_dependency(transition.target_state, util::to_pipeline_stage_flags_bitwise(transition.dependent_shaders));
        CC_ASSERT(after_dep != 0 && "Transition shader dependencies must be specified if transitioning to a CBV/SRV/UAV");

        resource_state before;
        VkPipelineStageFlags before_dep;
        bool before_known = _state_cache->transition_resource(transition.resource, transition.target_state, after_dep, before, before_dep);

        if (before_known && before != transition.target_state)
        {
            // The transition is neither the implicit initial one, nor redundant
            state_change const change = state_change(before, transition.target_state, before_dep, after_dep);

            // NOTE: in both cases we transition the entire resource (all subresources in D3D12 terms),
            // using stored information from the resource pool (img_info / buf_info respectively)
            if (_globals.pool_resources->isImage(transition.resource))
            {
                auto const& img_info = _globals.pool_resources->getImageInfo(transition.resource);
                barriers.add_image_barrier(img_info.raw_image, change, util::to_native_image_aspect(img_info.pixel_format));
            }
            else
            {
                auto const& buf_info = _globals.pool_resources->getBufferInfo(transition.resource);
                barriers.add_buffer_barrier(buf_info.raw_buffer, change, buf_info.width);
            }
        }
    }

    barriers.record(_cmd_list);
}

void phi::vk::command_list_translator::execute(const phi::cmd::transition_image_slices& transition_images)
{
    // Image slice transitions are entirely explicit, and require the user to synchronize before/after resource states
    // NOTE: we do not update the master state as it does not encompass subresource states

    barrier_bundle<limits::max_resource_transitions> barriers;

    for (auto const& transition : transition_images.transitions)
    {
        auto const before_dep
            = util::to_pipeline_stage_dependency(transition.source_state, util::to_pipeline_stage_flags_bitwise(transition.source_dependencies));
        auto const after_dep
            = util::to_pipeline_stage_dependency(transition.target_state, util::to_pipeline_stage_flags_bitwise(transition.target_dependencies));

        state_change const change = state_change(transition.source_state, transition.target_state, before_dep, after_dep);

        CC_ASSERT(_globals.pool_resources->isImage(transition.resource));
        auto const& img_info = _globals.pool_resources->getImageInfo(transition.resource);
        barriers.add_image_barrier(img_info.raw_image, change, util::to_native_image_aspect(img_info.pixel_format), uint32_t(transition.mip_level),
                                   uint32_t(transition.array_slice));
    }

    barriers.record(_cmd_list);

    for (auto const& state_reset : transition_images.state_resets)
    {
        auto const after_dep = util::to_pipeline_stage_dependency(state_reset.new_state, util::to_pipeline_stage_flags_bitwise(state_reset.new_dependencies));
        CC_ASSERT(after_dep != 0 && "Transition shader dependencies must be specified if transitioning to a CBV/SRV/UAV");

        resource_state before;
        VkPipelineStageFlags before_dep;
        bool before_known = _state_cache->transition_resource(state_reset.resource, state_reset.new_state, after_dep, before, before_dep);
        CC_ASSERT(before_known && "state resets require a locally known before-state. transition the resources normally before using slice transitions");
    }
}

void phi::vk::command_list_translator::execute(const phi::cmd::barrier_uav&)
{
    CC_ASSERT(_bound.raw_render_pass == nullptr && "Vulkan UAV barriers must not occur during render passes");
    // instead of using VkBuffer/ImageMemoryBarriers per resource, always issue a full memory barrier
    // implementations do not care about per-buffer restrictions according to sources

    VkMemoryBarrier desc = {};
    desc.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    desc.pNext = nullptr;
    desc.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    desc.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_SHADER_READ_BIT; // UAV to UAV

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    if (this->_globals.has_raytracing)
    {
        desc.srcAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        desc.dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        src_stage |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        dst_stage |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    }

    vkCmdPipelineBarrier(_cmd_list, src_stage, dst_stage, 0, 1u, &desc, 0, nullptr, 0, nullptr);
}

void phi::vk::command_list_translator::execute(const phi::cmd::copy_buffer& copy_buf)
{
    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(copy_buf.source, copy_buf.num_bytes) && "copy_buffer source OOB");
    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(copy_buf.destination, copy_buf.num_bytes) && "copy_buffer dest OOB");

    auto const src_buffer = _globals.pool_resources->getRawBuffer(copy_buf.source);
    auto const dest_buffer = _globals.pool_resources->getRawBuffer(copy_buf.destination);

    VkBufferCopy region = {};
    region.size = copy_buf.num_bytes;
    region.srcOffset = copy_buf.source.offset_bytes;
    region.dstOffset = copy_buf.destination.offset_bytes;
    vkCmdCopyBuffer(_cmd_list, src_buffer, dest_buffer, 1, &region);
}

void phi::vk::command_list_translator::execute(const phi::cmd::copy_texture& copy_text)
{
    auto const& src_image_info = _globals.pool_resources->getImageInfo(copy_text.source);
    auto const& dest_image_info = _globals.pool_resources->getImageInfo(copy_text.destination);

    VkImageCopy copy = {};
    copy.srcSubresource.aspectMask = util::to_native_image_aspect(src_image_info.pixel_format);
    copy.srcSubresource.baseArrayLayer = copy_text.src_array_index;
    copy.srcSubresource.layerCount = copy_text.num_array_slices;
    copy.srcSubresource.mipLevel = copy_text.src_mip_index;
    copy.dstSubresource.aspectMask = util::to_native_image_aspect(dest_image_info.pixel_format);
    copy.dstSubresource.baseArrayLayer = copy_text.dest_array_index;
    copy.dstSubresource.layerCount = copy_text.num_array_slices;
    copy.dstSubresource.mipLevel = copy_text.dest_mip_index;
    copy.extent.width = copy_text.width;
    copy.extent.height = copy_text.height;
    copy.extent.depth = 1;

    vkCmdCopyImage(_cmd_list, src_image_info.raw_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest_image_info.raw_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
}

void phi::vk::command_list_translator::execute(const phi::cmd::copy_buffer_to_texture& copy_text)
{
    auto const src_buffer = _globals.pool_resources->getRawBuffer(copy_text.source);
    auto const& dest_image_info = _globals.pool_resources->getImageInfo(copy_text.destination);

    VkBufferImageCopy region = {};
    region.bufferOffset = copy_text.source.offset_bytes;
    region.imageSubresource.aspectMask = util::to_native_image_aspect(dest_image_info.pixel_format);
    region.imageSubresource.baseArrayLayer = copy_text.dest_array_index;
    region.imageSubresource.layerCount = 1;
    region.imageSubresource.mipLevel = copy_text.dest_mip_index;
    region.imageExtent.width = copy_text.dest_width;
    region.imageExtent.height = copy_text.dest_height;
    region.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(_cmd_list, src_buffer, dest_image_info.raw_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void phi::vk::command_list_translator::execute(const phi::cmd::copy_texture_to_buffer& copy_text)
{
    auto const src_image = _globals.pool_resources->getRawImage(copy_text.source);
    auto const& src_image_info = _globals.pool_resources->getImageInfo(copy_text.destination.buffer);
    auto const dest_buffer = _globals.pool_resources->getRawBuffer(copy_text.destination.buffer);

    VkBufferImageCopy region = {};
    region.bufferOffset = copy_text.destination.offset_bytes;
    region.imageSubresource.aspectMask = util::to_native_image_aspect(src_image_info.pixel_format);
    region.imageSubresource.baseArrayLayer = copy_text.src_array_index;
    region.imageSubresource.layerCount = 1;
    region.imageSubresource.mipLevel = copy_text.src_mip_index;
    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;
    region.imageExtent.width = copy_text.src_width;
    region.imageExtent.height = copy_text.src_height;
    region.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(_cmd_list, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest_buffer, 1, &region);
}

void phi::vk::command_list_translator::execute(const phi::cmd::resolve_texture& resolve)
{
    constexpr auto src_layout = util::to_image_layout(resource_state::resolve_src);
    constexpr auto dest_layout = util::to_image_layout(resource_state::resolve_dest);

    auto const src_image = _globals.pool_resources->getRawImage(resolve.source);
    auto const dest_image = _globals.pool_resources->getRawImage(resolve.destination);

    auto const& dest_info = _globals.pool_resources->getImageInfo(resolve.destination);

    VkImageResolve region = {};

    region.srcSubresource.aspectMask = phi::util::is_depth_format(dest_info.pixel_format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = resolve.src_mip_index;
    region.srcSubresource.layerCount = 1;
    region.srcSubresource.baseArrayLayer = resolve.src_array_index;

    region.dstSubresource.aspectMask = region.srcSubresource.aspectMask;
    region.dstSubresource.mipLevel = resolve.dest_mip_index;
    region.dstSubresource.layerCount = 1;
    region.dstSubresource.baseArrayLayer = resolve.dest_array_index;
    region.srcOffset = {};
    region.dstOffset = {};
    region.extent.width = resolve.width;
    region.extent.height = resolve.height;
    region.extent.depth = 1;

    vkCmdResolveImage(_cmd_list, src_image, src_layout, dest_image, dest_layout, 1, &region);
}

void phi::vk::command_list_translator::execute(const phi::cmd::write_timestamp& timestamp)
{
    VkQueryPool pool;
    uint32_t const query_index = _globals.pool_queries->getQuery(timestamp.query_range, query_type::timestamp, timestamp.index, pool);

    // NOTE: this is likely wildly inefficient on some non-desktop IHV, revisit when necessary
    // it could be moved to the tail of cmd::resolve_queries without breaking API, which would at least reset in ranges > 1
    // however then we'd need an initial command list resetting all queries on backend launch, skipping that for now
    vkCmdResetQueryPool(_cmd_list, pool, query_index, 1);

    vkCmdWriteTimestamp(_cmd_list, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, query_index);
}

void phi::vk::command_list_translator::execute(const phi::cmd::resolve_queries& resolve)
{
    query_type type;
    VkQueryPool raw_pool;
    uint32_t const query_index_start = _globals.pool_queries->getQuery(resolve.src_query_range, resolve.query_start, raw_pool, type);

    VkBuffer const raw_dest_buffer = _globals.pool_resources->getRawBuffer(resolve.destination);

    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(resolve.destination, resolve.num_queries * sizeof(uint64_t))
              && "resolve query destination buffer accessed OOB");
    VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT;
    vkCmdCopyQueryPoolResults(_cmd_list, raw_pool, query_index_start, resolve.num_queries, raw_dest_buffer, resolve.destination.offset_bytes,
                              sizeof(uint64_t), flags);
}

void phi::vk::command_list_translator::execute(const phi::cmd::begin_debug_label& label)
{
    // TODO: it's currently not a hard error if this extension is missing
    // resolve this depending on availability of vk_ext_debug_utils
    CC_ASSERT(vkCmdBeginDebugUtilsLabelEXT != nullptr && "cmd::begin_debug_label not available, contact maintainers");

    VkDebugUtilsLabelEXT info = {};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    info.pLabelName = label.string;

    vkCmdBeginDebugUtilsLabelEXT(_cmd_list, &info);
}

void phi::vk::command_list_translator::execute(const phi::cmd::end_debug_label&)
{
    // see execute(begin_debug_label);
    CC_ASSERT(vkCmdBeginDebugUtilsLabelEXT != nullptr && "cmd::end_debug_label not available, contact maintainers");
    vkCmdEndDebugUtilsLabelEXT(_cmd_list);
}

void phi::vk::command_list_translator::execute(cmd::begin_profile_scope const& scope)
{
    (void)scope;

#ifdef PHI_HAS_OPTICK
    if (_current_optick_event)
    {
        Optick::GPUEvent::Stop(*_current_optick_event);
        _current_optick_event = nullptr;
    }

    if (scope.optick_event)
    {
        _current_optick_event = Optick::GPUEvent::Start(*scope.optick_event);
    }
#endif
}

void phi::vk::command_list_translator::execute(cmd::end_profile_scope const&)
{
#ifdef PHI_HAS_OPTICK
    if (_current_optick_event)
    {
        Optick::GPUEvent::Stop(*_current_optick_event);
        _current_optick_event = nullptr;
    }
#endif
}

void phi::vk::command_list_translator::execute(const phi::cmd::update_bottom_level& blas_update)
{
    auto& dest_node = _globals.pool_accel_structs->getNode(blas_update.dest);
    auto const src = blas_update.source.is_valid() ? _globals.pool_accel_structs->getNode(blas_update.source).raw_as : nullptr;
    auto const dest_scratch = _globals.pool_resources->getRawBuffer(dest_node.buffer_scratch);

    VkAccelerationStructureInfoNV build_info = {};
    build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
    build_info.pNext = nullptr;
    build_info.flags = util::to_native_accel_struct_build_flags(dest_node.flags);
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
    build_info.geometryCount = uint32_t(dest_node.geometries.size());
    build_info.pGeometries = dest_node.geometries.empty() ? nullptr : dest_node.geometries.data();
    build_info.instanceCount = 0;

    vkCmdBuildAccelerationStructureNV(_cmd_list, &build_info, nullptr, 0, (src == nullptr) ? VK_FALSE : VK_TRUE, dest_node.raw_as, src, dest_scratch, 0);

    VkMemoryBarrier mem_barrier = {};
    mem_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mem_barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;
    mem_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;

    vkCmdPipelineBarrier(_cmd_list, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0,
                         1, &mem_barrier, 0, nullptr, 0, nullptr);
}

void phi::vk::command_list_translator::execute(const phi::cmd::update_top_level& tlas_update)
{
    auto& dest_node = _globals.pool_accel_structs->getNode(tlas_update.dest_accel_struct);
    auto const dest_scratch = _globals.pool_resources->getRawBuffer(dest_node.buffer_scratch);

    VkAccelerationStructureInfoNV build_info = {};
    build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
    build_info.pNext = nullptr;
    build_info.flags = util::to_native_accel_struct_build_flags(dest_node.flags);
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    build_info.geometryCount = 0;
    build_info.pGeometries = nullptr;
    build_info.instanceCount = tlas_update.num_instances;

    vkCmdBuildAccelerationStructureNV(_cmd_list, &build_info, _globals.pool_resources->getRawBuffer(tlas_update.source_instances_addr.buffer),
                                      tlas_update.source_instances_addr.offset_bytes, VK_FALSE, dest_node.raw_as, nullptr, dest_scratch, 0);

    VkMemoryBarrier mem_barrier = {};
    mem_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mem_barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;
    mem_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;

    vkCmdPipelineBarrier(_cmd_list, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0,
                         1, &mem_barrier, 0, nullptr, 0, nullptr);
}

void phi::vk::command_list_translator::execute(const cmd::dispatch_rays& dispatch_rays)
{
    auto const& pso_node = _globals.pool_pipeline_states->get(dispatch_rays.pso);

    if (_bound.update_pso(dispatch_rays.pso))
    {
        _bound.update_pipeline_layout(pso_node.associated_pipeline_layout->raw_layout);
        vkCmdBindPipeline(_cmd_list, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, pso_node.raw_pipeline);
    }

    VkBuffer const raygen_buf = get_buffer_or_null(dispatch_rays.table_ray_generation.buffer);
    VkBuffer const miss_buf = get_buffer_or_null(dispatch_rays.table_miss.buffer);
    VkBuffer const hitgrp_buf = get_buffer_or_null(dispatch_rays.table_hit_groups.buffer);
    VkBuffer const callable_buf = get_buffer_or_null(dispatch_rays.table_callable.buffer);


    vkCmdTraceRaysNV(_cmd_list,                                                                                            //
                     raygen_buf, dispatch_rays.table_ray_generation.offset_bytes,                                          //
                     miss_buf, dispatch_rays.table_miss.offset_bytes, dispatch_rays.table_miss.stride_bytes,               //
                     hitgrp_buf, dispatch_rays.table_hit_groups.offset_bytes, dispatch_rays.table_hit_groups.stride_bytes, //
                     callable_buf, dispatch_rays.table_callable.offset_bytes, dispatch_rays.table_callable.stride_bytes,   //
                     dispatch_rays.dispatch_x, dispatch_rays.dispatch_y, dispatch_rays.dispatch_z);
}

void phi::vk::command_list_translator::execute(const phi::cmd::clear_textures& clear_tex)
{
    for (uint8_t i = 0u; i < clear_tex.clear_ops.size(); ++i)
    {
        auto const& op = clear_tex.clear_ops[i];
        auto* const resource = _globals.pool_resources->getRawImage(op.rv.resource);

        VkImageSubresourceRange range = {};
        range.aspectMask = util::to_native_image_aspect(op.rv.texture_info.pixel_format);
        range.baseMipLevel = op.rv.texture_info.mip_start;
        range.levelCount = op.rv.texture_info.mip_size;
        range.baseArrayLayer = op.rv.texture_info.array_start;
        range.layerCount = op.rv.texture_info.array_size;


        if (phi::util::is_depth_format(op.rv.texture_info.pixel_format))
        {
            VkClearDepthStencilValue clearval = {};
            clearval.depth = op.value.red_or_depth / 255.f;
            clearval.stencil = op.value.green_or_stencil;
            vkCmdClearDepthStencilImage(_cmd_list, resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearval, 1, &range);
        }
        else
        {
            VkClearColorValue clearval = {};
            clearval.float32[0] = op.value.red_or_depth / 255.f;
            clearval.float32[1] = op.value.green_or_stencil / 255.f;
            clearval.float32[2] = op.value.blue / 255.f;
            clearval.float32[3] = op.value.alpha / 255.f;
            vkCmdClearColorImage(_cmd_list, resource, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearval, 1, &range);
        }
    }
}

void phi::vk::command_list_translator::execute(cmd::code_location_marker const& marker)
{
    _last_code_location.file = marker.file;
    _last_code_location.function = marker.function;
    _last_code_location.line = marker.line;
}

void phi::vk::command_list_translator::bind_vertex_buffers(handle::resource const vertex_buffers[limits::max_vertex_buffers])
{
    uint64_t const vert_hash = phi::util::sse_hash_type<handle::resource>(vertex_buffers, limits::max_vertex_buffers);
    if (vert_hash != _bound.vertex_buffer_hash)
    {
        _bound.vertex_buffer_hash = vert_hash;
        if (vertex_buffers[0].is_valid())
        {
            VkBuffer raw_buffers[limits::max_vertex_buffers];
            VkDeviceSize offsets[limits::max_vertex_buffers] = {};
            uint32_t numVertexBuffers = 0;

            for (auto i = 0u; i < limits::max_vertex_buffers; ++i)
            {
                if (!vertex_buffers[i].is_valid())
                    break;

                raw_buffers[i] = _globals.pool_resources->getRawBuffer(vertex_buffers[i]);
                ++numVertexBuffers;
            }

            vkCmdBindVertexBuffers(_cmd_list, 0, numVertexBuffers, raw_buffers, offsets);
        }
    }
}

void phi::vk::command_list_translator::bind_shader_arguments(phi::handle::pipeline_state pso,
                                                             const std::byte* root_consts,
                                                             cc::span<const phi::shader_argument> shader_args,
                                                             VkPipelineBindPoint bind_point)
{
    auto const& pso_node = _globals.pool_pipeline_states->get(pso);
    pipeline_layout const& pipeline_layout = *pso_node.associated_pipeline_layout;

    if (pipeline_layout.has_push_constants())
    {
        static_assert(sizeof(cmd::draw::root_constants) == sizeof(std::byte[limits::max_root_constant_bytes]), "root constants have wrong size");
        static_assert(sizeof(cmd::draw::root_constants) == sizeof(cmd::dispatch::root_constants), "root constants have wrong size");

        vkCmdPushConstants(_cmd_list, pipeline_layout.raw_layout, pipeline_layout.push_constant_stages, 0,
                           sizeof(std::byte[limits::max_root_constant_bytes]), root_consts);
    }

    for (uint8_t i = 0; i < shader_args.size(); ++i)
    {
        auto& bound_arg = _bound.shader_args[i];
        auto const& arg = shader_args[i];

        if (arg.constant_buffer.is_valid())
        {
            if (bound_arg.update_cbv(arg.constant_buffer, arg.constant_buffer_offset))
            {
                CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(arg.constant_buffer, arg.constant_buffer_offset, 1) && "CBV offset OOB");

                auto const cbv_desc_set = bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS
                                              ? _globals.pool_resources->getRawCBVDescriptorSet(arg.constant_buffer)
                                              : _globals.pool_resources->getRawCBVDescriptorSetCompute(arg.constant_buffer);
                vkCmdBindDescriptorSets(_cmd_list, bind_point, pipeline_layout.raw_layout, i + limits::max_shader_arguments, 1, &cbv_desc_set, 1,
                                        &arg.constant_buffer_offset);
            }
        }

        // Set the shader view if it has changed
        if (bound_arg.update_shader_view(arg.shader_view))
        {
            if (arg.shader_view.is_valid())
            {
                auto const sv_desc_set = _globals.pool_shader_views->get(arg.shader_view);
                vkCmdBindDescriptorSets(_cmd_list, bind_point, pipeline_layout.raw_layout, i, 1, &sv_desc_set, 0, nullptr);
            }
        }
    }
}

VkBuffer phi::vk::command_list_translator::get_buffer_or_null(phi::handle::resource buf) const
{
    if (!buf.is_valid())
        return nullptr;

    return _globals.pool_resources->getRawBuffer(buf);
}
