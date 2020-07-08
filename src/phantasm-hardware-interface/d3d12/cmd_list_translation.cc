#include "cmd_list_translation.hh"

#include <phantasm-hardware-interface/detail/byte_util.hh>
#include <phantasm-hardware-interface/detail/command_reading.hh>
#include <phantasm-hardware-interface/detail/format_size.hh>
#include <phantasm-hardware-interface/detail/incomplete_state_cache.hh>
#include <phantasm-hardware-interface/detail/log.hh>

#include "Swapchain.hh"

#include "common/diagnostic_util.hh"
#include "common/dxgi_format.hh"
#include "common/native_enum.hh"
#include "common/shared_com_ptr.hh"
#include "common/util.hh"

#include "pools/accel_struct_pool.hh"
#include "pools/cmd_list_pool.hh"
#include "pools/pso_pool.hh"
#include "pools/query_pool.hh"
#include "pools/resource_pool.hh"
#include "pools/root_sig_cache.hh"
#include "pools/shader_view_pool.hh"

void phi::d3d12::command_list_translator::initialize(
    ID3D12Device* device, ShaderViewPool* sv_pool, ResourcePool* resource_pool, PipelineStateObjectPool* pso_pool, AccelStructPool* as_pool, QueryPool* query_pool)
{
    _globals.initialize(device, sv_pool, resource_pool, pso_pool, as_pool, query_pool);
    _thread_local.initialize(*_globals.device);
}

void phi::d3d12::command_list_translator::translateCommandList(
    ID3D12GraphicsCommandList5* list, queue_type type, d3d12_incomplete_state_cache* state_cache, std::byte* buffer, size_t buffer_size)
{
    _cmd_list = list;
    _current_queue_type = type;
    _state_cache = state_cache;

    _bound.reset();
    _state_cache->reset();

    auto const gpu_heaps = _globals.pool_shader_views->getGPURelevantHeaps();
    _cmd_list->SetDescriptorHeaps(UINT(gpu_heaps.size()), gpu_heaps.data());

    // translate all contained commands
    command_stream_parser parser(buffer, buffer_size);
    for (auto const& cmd : parser)
        cmd::detail::dynamic_dispatch(cmd, *this);

    // close the list
    _cmd_list->Close();

    // done
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::begin_render_pass& begin_rp)
{
    CC_ASSERT(_current_queue_type == queue_type::direct && "graphics commands are only valid on queue_type::direct");

    // depthrange is hardcoded to [0, 1]
    auto const viewport = D3D12_VIEWPORT{float(begin_rp.viewport_offset.x),
                                         float(begin_rp.viewport_offset.y),
                                         float(begin_rp.viewport.width),
                                         float(begin_rp.viewport.height),
                                         0.f,
                                         1.f};

    // by default, set scissor exactly to viewport
    auto const scissor_rect
        = D3D12_RECT{0, 0, LONG(begin_rp.viewport.width + begin_rp.viewport_offset.x), LONG(begin_rp.viewport.height + begin_rp.viewport_offset.y)};

    _cmd_list->RSSetViewports(1, &viewport);
    _cmd_list->RSSetScissorRects(1, &scissor_rect);

    resource_view_cpu_only const dynamic_rtvs = _thread_local.lin_alloc_rtvs.allocate(begin_rp.render_targets.size());

    for (uint8_t i = 0; i < begin_rp.render_targets.size(); ++i)
    {
        auto const& rt = begin_rp.render_targets[i];

        auto* const resource = _globals.pool_resources->getRawResource(rt.rv.resource);
        auto const rtv = dynamic_rtvs.get_index(i);

        // create the default RTV on the fly
        if (_globals.pool_resources->isBackbuffer(rt.rv.resource))
        {
            // Create a default RTV for the backbuffer
            _globals.device->CreateRenderTargetView(resource, nullptr, rtv);
        }
        else
        {
            // Create an RTV based on the supplied info
            auto const rtv_desc = util::create_rtv_desc(rt.rv);
            _globals.device->CreateRenderTargetView(resource, &rtv_desc, rtv);
        }

        if (rt.clear_type == rt_clear_type::clear)
        {
            _cmd_list->ClearRenderTargetView(rtv, rt.clear_value, 0, nullptr);
        }
    }

    resource_view_cpu_only dynamic_dsv;
    if (begin_rp.depth_target.rv.resource.is_valid())
    {
        dynamic_dsv = _thread_local.lin_alloc_dsvs.allocate(1u);
        auto* const resource = _globals.pool_resources->getRawResource(begin_rp.depth_target.rv.resource);

        // Create an DSV based on the supplied info
        auto const dsv_desc = util::create_dsv_desc(begin_rp.depth_target.rv);
        _globals.device->CreateDepthStencilView(resource, &dsv_desc, dynamic_dsv.get_start());

        if (begin_rp.depth_target.clear_type == rt_clear_type::clear)
        {
            _cmd_list->ClearDepthStencilView(dynamic_dsv.get_start(), D3D12_CLEAR_FLAG_DEPTH, begin_rp.depth_target.clear_value_depth,
                                             begin_rp.depth_target.clear_value_stencil, 0, nullptr);
        }
    }

    // set the render targets
    _cmd_list->OMSetRenderTargets(begin_rp.render_targets.size(), begin_rp.render_targets.size() > 0 ? &dynamic_rtvs.get_start() : nullptr, true,
                                  dynamic_dsv.is_valid() ? &dynamic_dsv.get_start() : nullptr);

    // reset the linear allocators
    _thread_local.lin_alloc_rtvs.reset();
    _thread_local.lin_alloc_dsvs.reset();
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::draw& draw)
{
    CC_ASSERT(_current_queue_type == queue_type::direct && "graphics commands are only valid on queue_type::direct");
    auto const& pso_node = _globals.pool_pipeline_states->get(draw.pipeline_state);

    // PSO
    if (_bound.update_pso(draw.pipeline_state))
    {
        _cmd_list->SetPipelineState(pso_node.raw_pso);
        _cmd_list->IASetPrimitiveTopology(pso_node.primitive_topology);
    }

    // Root signature
    if (_bound.update_root_sig(pso_node.associated_root_sig->raw_root_sig))
    {
        _cmd_list->SetGraphicsRootSignature(_bound.raw_root_sig);
    }

    // Index buffer (optional)
    if (draw.index_buffer != _bound.index_buffer)
    {
        _bound.index_buffer = draw.index_buffer;
        if (draw.index_buffer.is_valid())
        {
            auto const ibv = _globals.pool_resources->getIndexBufferView(draw.index_buffer);
            _cmd_list->IASetIndexBuffer(&ibv);
        }
    }

    // Vertex buffer
    if (draw.vertex_buffer != _bound.vertex_buffer)
    {
        _bound.vertex_buffer = draw.vertex_buffer;
        if (draw.vertex_buffer.is_valid())
        {
            auto const vbv = _globals.pool_resources->getVertexBufferView(draw.vertex_buffer);
            _cmd_list->IASetVertexBuffers(0, 1, &vbv);
        }
    }

    // Shader arguments
    {
        auto const& root_sig = *pso_node.associated_root_sig;

        // root constants
        if (!root_sig.argument_maps.empty() && root_sig.argument_maps[0].root_const_param != unsigned(-1))
        {
            static_assert(sizeof(draw.root_constants) % sizeof(DWORD32) == 0, "root constant size not divisible by dword32 size");
            _cmd_list->SetGraphicsRoot32BitConstants(root_sig.argument_maps[0].root_const_param, sizeof(draw.root_constants) / sizeof(DWORD32),
                                                     draw.root_constants, 0);
        }

        for (uint8_t i = 0; i < root_sig.argument_maps.size(); ++i)
        {
            auto& bound_arg = _bound.shader_args[i];
            auto const& arg = draw.shader_arguments[i];
            auto const& map = root_sig.argument_maps[i];

            if (map.cbv_param != uint32_t(-1))
            {
                // Set the CBV / offset if it has changed
                if (bound_arg.update_cbv(arg.constant_buffer, arg.constant_buffer_offset))
                {
                    auto const cbv = _globals.pool_resources->getConstantBufferView(arg.constant_buffer);
                    _cmd_list->SetGraphicsRootConstantBufferView(map.cbv_param, cbv.BufferLocation + arg.constant_buffer_offset);
                }
            }

            // Set the shader view if it has changed
            if (bound_arg.update_shader_view(arg.shader_view))
            {
                if (map.srv_uav_table_param != uint32_t(-1))
                {
                    auto const sv_desc_table = _globals.pool_shader_views->getSRVUAVGPUHandle(arg.shader_view);
                    _cmd_list->SetGraphicsRootDescriptorTable(map.srv_uav_table_param, sv_desc_table);
                }

                if (map.sampler_table_param != uint32_t(-1))
                {
                    auto const sampler_desc_table = _globals.pool_shader_views->getSamplerGPUHandle(arg.shader_view);
                    _cmd_list->SetGraphicsRootDescriptorTable(map.sampler_table_param, sampler_desc_table);
                }
            }
        }
    }

    if (draw.scissor.min.x != -1)
    {
        D3D12_RECT scissor_rect = {draw.scissor.min.x, draw.scissor.min.y, draw.scissor.max.x, draw.scissor.max.y};
        _cmd_list->RSSetScissorRects(1, &scissor_rect);
    }

    // Draw command
    if (draw.index_buffer.is_valid())
    {
        _cmd_list->DrawIndexedInstanced(draw.num_indices, 1, draw.index_offset, draw.vertex_offset, 0);
    }
    else
    {
        _cmd_list->DrawInstanced(draw.num_indices, 1, draw.vertex_offset, 0);
    }
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::draw_indirect& draw_indirect)
{
    CC_ASSERT(_current_queue_type == queue_type::direct && "graphics commands are only valid on queue_type::direct");
    auto const& pso_node = _globals.pool_pipeline_states->get(draw_indirect.pipeline_state);

    // PSO
    if (_bound.update_pso(draw_indirect.pipeline_state))
    {
        _cmd_list->SetPipelineState(pso_node.raw_pso);
    }

    // Root signature
    if (_bound.update_root_sig(pso_node.associated_root_sig->raw_root_sig))
    {
        _cmd_list->SetGraphicsRootSignature(_bound.raw_root_sig);
        _cmd_list->IASetPrimitiveTopology(pso_node.primitive_topology);
    }

    // Index buffer (optional)
    if (draw_indirect.index_buffer != _bound.index_buffer)
    {
        _bound.index_buffer = draw_indirect.index_buffer;
        if (draw_indirect.index_buffer.is_valid())
        {
            auto const ibv = _globals.pool_resources->getIndexBufferView(draw_indirect.index_buffer);
            _cmd_list->IASetIndexBuffer(&ibv);
        }
    }

    // Vertex buffer
    if (draw_indirect.vertex_buffer != _bound.vertex_buffer)
    {
        _bound.vertex_buffer = draw_indirect.vertex_buffer;
        if (draw_indirect.vertex_buffer.is_valid())
        {
            auto const vbv = _globals.pool_resources->getVertexBufferView(draw_indirect.vertex_buffer);
            _cmd_list->IASetVertexBuffers(0, 1, &vbv);
        }
    }

    // Shader arguments
    {
        auto const& root_sig = *pso_node.associated_root_sig;

        // root constants
        if (!root_sig.argument_maps.empty() && root_sig.argument_maps[0].root_const_param != unsigned(-1))
        {
            static_assert(sizeof(draw_indirect.root_constants) % sizeof(DWORD32) == 0, "root constant size not divisible by dword32 size");
            _cmd_list->SetGraphicsRoot32BitConstants(root_sig.argument_maps[0].root_const_param,
                                                     sizeof(draw_indirect.root_constants) / sizeof(DWORD32), draw_indirect.root_constants, 0);
        }

        for (uint8_t i = 0; i < draw_indirect.shader_arguments.size(); ++i)
        {
            auto& bound_arg = _bound.shader_args[i];
            auto const& arg = draw_indirect.shader_arguments[i];
            auto const& map = root_sig.argument_maps[i];

            if (map.cbv_param != uint32_t(-1))
            {
                // Set the CBV / offset if it has changed
                if (bound_arg.update_cbv(arg.constant_buffer, arg.constant_buffer_offset))
                {
                    auto const cbv = _globals.pool_resources->getConstantBufferView(arg.constant_buffer);
                    _cmd_list->SetGraphicsRootConstantBufferView(map.cbv_param, cbv.BufferLocation + arg.constant_buffer_offset);
                }
            }

            // Set the shader view if it has changed
            if (bound_arg.update_shader_view(arg.shader_view))
            {
                if (map.srv_uav_table_param != uint32_t(-1))
                {
                    auto const sv_desc_table = _globals.pool_shader_views->getSRVUAVGPUHandle(arg.shader_view);
                    _cmd_list->SetGraphicsRootDescriptorTable(map.srv_uav_table_param, sv_desc_table);
                }

                if (map.sampler_table_param != uint32_t(-1))
                {
                    auto const sampler_desc_table = _globals.pool_shader_views->getSamplerGPUHandle(arg.shader_view);
                    _cmd_list->SetGraphicsRootDescriptorTable(map.sampler_table_param, sampler_desc_table);
                }
            }
        }
    }


    ID3D12Resource* const raw_arg_buffer = _globals.pool_resources->getRawResource(draw_indirect.indirect_argument_buffer);
    ID3D12CommandSignature* const comsig = draw_indirect.index_buffer.is_valid() ? _globals.pool_pipeline_states->getGlobalComSigDrawIndexed()
                                                                                 : _globals.pool_pipeline_states->getGlobalComSigDraw();

    // NOTE: We use no count buffer, which makes the second argument determine the actual amount of args, not the max
    // NOTE: One of two global command sigs are used, containing 256 draw / draw_indexed argument types each
    // as only those two arg types are used, they require no association with a rootsig making things a lot simpler
    // the amount of arguments configured in those rootsigs is more or less arbitrary, could be increased possibly by a lot without cost
    CC_ASSERT(draw_indirect.num_arguments <= 256 && "Too many indirect arguments, contact maintainers");
    _cmd_list->ExecuteIndirect(comsig, draw_indirect.num_arguments, raw_arg_buffer, draw_indirect.argument_buffer_offset, nullptr, 0);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::dispatch& dispatch)
{
    auto const& pso_node = _globals.pool_pipeline_states->get(dispatch.pipeline_state);

    // PSO
    if (_bound.update_pso(dispatch.pipeline_state))
    {
        _cmd_list->SetPipelineState(pso_node.raw_pso);
    }

    // Root signature
    if (_bound.update_root_sig(pso_node.associated_root_sig->raw_root_sig))
    {
        _cmd_list->SetComputeRootSignature(_bound.raw_root_sig);
    }

    // Shader arguments
    {
        auto const& root_sig = *pso_node.associated_root_sig;

        // root constants
        auto const root_constant_param = root_sig.argument_maps[0].root_const_param;
        if (root_constant_param != unsigned(-1))
        {
            static_assert(sizeof(dispatch.root_constants) % sizeof(DWORD32) == 0, "root constant size not divisible by dword32 size");
            _cmd_list->SetComputeRoot32BitConstants(root_constant_param, sizeof(dispatch.root_constants) / sizeof(DWORD32), dispatch.root_constants, 0);
        }

        // regular shader arguments
        for (uint8_t i = 0; i < dispatch.shader_arguments.size(); ++i)
        {
            auto& bound_arg = _bound.shader_args[i];
            auto const& arg = dispatch.shader_arguments[i];
            auto const& map = root_sig.argument_maps[i];


            if (map.cbv_param != uint32_t(-1))
            {
                // Set the CBV / offset if it has changed
                if (bound_arg.update_cbv(arg.constant_buffer, arg.constant_buffer_offset))
                {
                    auto const cbv = _globals.pool_resources->getConstantBufferView(arg.constant_buffer);
                    _cmd_list->SetComputeRootConstantBufferView(map.cbv_param, cbv.BufferLocation + arg.constant_buffer_offset);
                }
            }

            // Set the shader view if it has changed
            if (bound_arg.update_shader_view(arg.shader_view))
            {
                if (map.srv_uav_table_param != uint32_t(-1))
                {
                    auto const sv_desc_table = _globals.pool_shader_views->getSRVUAVGPUHandle(arg.shader_view);
                    _cmd_list->SetComputeRootDescriptorTable(map.srv_uav_table_param, sv_desc_table);
                }

                if (map.sampler_table_param != uint32_t(-1))
                {
                    auto const sampler_desc_table = _globals.pool_shader_views->getSamplerGPUHandle(arg.shader_view);
                    _cmd_list->SetComputeRootDescriptorTable(map.sampler_table_param, sampler_desc_table);
                }
            }
        }
    }

    // Dispatch command
    _cmd_list->Dispatch(dispatch.dispatch_x, dispatch.dispatch_y, dispatch.dispatch_z);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::end_render_pass&)
{
    CC_ASSERT(_current_queue_type == queue_type::direct && "graphics commands are only valid on queue_type::direct");
    // do nothing
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::transition_resources& transition_res)
{
    cc::capped_vector<D3D12_RESOURCE_BARRIER, limits::max_resource_transitions> barriers;

    for (auto const& transition : transition_res.transitions)
    {
        D3D12_RESOURCE_STATES const after = util::to_native(transition.target_state);
        D3D12_RESOURCE_STATES before;

        bool const before_known = _state_cache->transition_resource(transition.resource, after, before);

        if (before_known && before != after)
        {
            // The transition is neither the implicit initial one, nor redundant
            barriers.push_back(util::get_barrier_desc(_globals.pool_resources->getRawResource(transition.resource), before, after));
        }
    }

    if (!barriers.empty())
    {
        _cmd_list->ResourceBarrier(UINT(barriers.size()), barriers.data());
    }
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::transition_image_slices& transition_images)
{
    // Image slice transitions are entirely explicit, and require the user to synchronize before/after resource states
    // NOTE: we do not update the master state as it does not encompass subresource states

    cc::capped_vector<D3D12_RESOURCE_BARRIER, limits::max_resource_transitions> barriers;
    for (auto const& transition : transition_images.transitions)
    {
        CC_ASSERT(_globals.pool_resources->isImage(transition.resource));
        auto const& img_info = _globals.pool_resources->getImageInfo(transition.resource);
        barriers.push_back(util::get_barrier_desc(_globals.pool_resources->getRawResource(transition.resource), util::to_native(transition.source_state),
                                                  util::to_native(transition.target_state), transition.mip_level, transition.array_slice, img_info.num_mips));
    }

    if (!barriers.empty())
    {
        _cmd_list->ResourceBarrier(UINT(barriers.size()), barriers.data());
    }
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::copy_buffer& copy_buf)
{
    _cmd_list->CopyBufferRegion(_globals.pool_resources->getRawResource(copy_buf.destination), copy_buf.dest_offset,
                                _globals.pool_resources->getRawResource(copy_buf.source), copy_buf.source_offset, copy_buf.size);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::copy_texture& copy_text)
{
    auto const& src_info = _globals.pool_resources->getImageInfo(copy_text.source);
    auto const& dest_info = _globals.pool_resources->getImageInfo(copy_text.destination);

    for (auto array_offset = 0u; array_offset < copy_text.num_array_slices; ++array_offset)
    {
        auto const src_subres_index = copy_text.src_mip_index + (copy_text.src_array_index + array_offset) * src_info.num_mips;
        auto const dest_subres_index = copy_text.dest_mip_index + (copy_text.dest_array_index + array_offset) * dest_info.num_mips;

        CD3DX12_TEXTURE_COPY_LOCATION const source(_globals.pool_resources->getRawResource(copy_text.source), src_subres_index);
        CD3DX12_TEXTURE_COPY_LOCATION const dest(_globals.pool_resources->getRawResource(copy_text.destination), dest_subres_index);
        _cmd_list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
    }
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::copy_buffer_to_texture& copy_text)
{
    auto const& dest_info = _globals.pool_resources->getImageInfo(copy_text.destination);
    auto const pixel_bytes = phi::detail::format_size_bytes(dest_info.pixel_format);
    auto const format_dxgi = util::to_dxgi_format(dest_info.pixel_format);

    D3D12_SUBRESOURCE_FOOTPRINT footprint;
    footprint.Format = format_dxgi;
    footprint.Width = copy_text.dest_width;
    footprint.Height = copy_text.dest_height;
    footprint.Depth = 1;
    footprint.RowPitch = mem::align_up(pixel_bytes * copy_text.dest_width, 256);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed_footprint;
    placed_footprint.Offset = copy_text.source_offset;
    placed_footprint.Footprint = footprint;

    auto const subres_index = copy_text.dest_mip_index + copy_text.dest_array_index * dest_info.num_mips;

    CD3DX12_TEXTURE_COPY_LOCATION const source(_globals.pool_resources->getRawResource(copy_text.source), placed_footprint);
    CD3DX12_TEXTURE_COPY_LOCATION const dest(_globals.pool_resources->getRawResource(copy_text.destination), subres_index);
    _cmd_list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::copy_texture_to_buffer& copy_text)
{
    auto const& src_info = _globals.pool_resources->getImageInfo(copy_text.source);

    D3D12_SUBRESOURCE_FOOTPRINT footprint;
    footprint.Format = util::to_dxgi_format(src_info.pixel_format);
    footprint.Width = copy_text.src_width;
    footprint.Height = copy_text.src_height;
    footprint.Depth = 1;
    footprint.RowPitch = mem::align_up(phi::detail::format_size_bytes(src_info.pixel_format) * copy_text.src_width, 256);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT dest_placed_footprint;
    dest_placed_footprint.Offset = copy_text.dest_offset;
    dest_placed_footprint.Footprint = footprint;

    auto const source_subres_index = copy_text.src_mip_index + copy_text.src_array_index * src_info.num_mips;

    CD3DX12_TEXTURE_COPY_LOCATION const source(_globals.pool_resources->getRawResource(copy_text.source), source_subres_index);
    CD3DX12_TEXTURE_COPY_LOCATION const dest(_globals.pool_resources->getRawResource(copy_text.destination), dest_placed_footprint);
    _cmd_list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::resolve_texture& resolve)
{
    auto const src_raw = _globals.pool_resources->getRawResource(resolve.source);
    auto const dest_raw = _globals.pool_resources->getRawResource(resolve.destination);

    auto const& src_info = _globals.pool_resources->getImageInfo(resolve.source);
    auto const& dest_info = _globals.pool_resources->getImageInfo(resolve.destination);
    auto const src_subres_index = resolve.src_mip_index + resolve.src_array_index * src_info.num_mips;
    auto const dest_subres_index = resolve.dest_mip_index + resolve.dest_array_index * dest_info.num_mips;

    _cmd_list->ResolveSubresource(dest_raw, dest_subres_index, src_raw, src_subres_index, util::to_dxgi_format(dest_info.pixel_format));
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::write_timestamp& timestamp)
{
    ID3D12QueryHeap* heap;
    UINT const query_index = _globals.pool_queries->getQuery(timestamp.query_range, query_type::timestamp, timestamp.index, heap);

    _cmd_list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, query_index);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::resolve_queries& resolve)
{
    query_type type;
    ID3D12QueryHeap* heap;
    UINT const query_index_start = _globals.pool_queries->getQuery(resolve.src_query_range, resolve.query_start, heap, type);

    ID3D12Resource* const raw_dest_buffer = _globals.pool_resources->getRawResource(resolve.dest_buffer);
    _cmd_list->ResolveQueryData(heap, util::to_query_type(type), query_index_start, resolve.num_queries, raw_dest_buffer, resolve.dest_offset);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::begin_debug_label& label) { util::begin_pix_marker(_cmd_list, 0, label.string); }

void phi::d3d12::command_list_translator::execute(const phi::cmd::end_debug_label&) { util::end_pix_marker(_cmd_list); }

void phi::d3d12::command_list_translator::execute(const phi::cmd::update_bottom_level& blas_update)
{
    auto& dest_node = _globals.pool_accel_structs->getNode(blas_update.dest);
    ID3D12Resource* const dest_as_buffer = _globals.pool_resources->getRawResource(dest_node.buffer_as);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC as_create_info = {};
    as_create_info.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    as_create_info.Inputs.Flags = util::to_native_flags(dest_node.flags);
    as_create_info.Inputs.NumDescs = static_cast<UINT>(dest_node.geometries.size());
    as_create_info.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    as_create_info.Inputs.pGeometryDescs = dest_node.geometries.empty() ? nullptr : dest_node.geometries.data();
    as_create_info.DestAccelerationStructureData = dest_as_buffer->GetGPUVirtualAddress();
    as_create_info.ScratchAccelerationStructureData = _globals.pool_resources->getRawResource(dest_node.buffer_scratch)->GetGPUVirtualAddress();

    _cmd_list->BuildRaytracingAccelerationStructure(&as_create_info, 0, nullptr);

    auto const uav_barrier = CD3DX12_RESOURCE_BARRIER::UAV(dest_as_buffer);
    _cmd_list->ResourceBarrier(1, &uav_barrier);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::update_top_level& tlas_update)
{
    auto& dest_node = _globals.pool_accel_structs->getNode(tlas_update.dest);
    ID3D12Resource* const dest_as_buffer = _globals.pool_resources->getRawResource(dest_node.buffer_as);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC as_create_info = {};
    as_create_info.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    as_create_info.Inputs.Flags = util::to_native_flags(dest_node.flags);
    as_create_info.Inputs.NumDescs = tlas_update.num_instances;
    as_create_info.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    as_create_info.Inputs.pGeometryDescs = nullptr;
    as_create_info.Inputs.InstanceDescs = _globals.pool_resources->getRawResource(dest_node.buffer_instances)->GetGPUVirtualAddress();
    as_create_info.DestAccelerationStructureData = dest_node.raw_as_handle;
    as_create_info.ScratchAccelerationStructureData = _globals.pool_resources->getRawResource(dest_node.buffer_scratch)->GetGPUVirtualAddress();

    _cmd_list->BuildRaytracingAccelerationStructure(&as_create_info, 0, nullptr);

    //    auto const uav_barrier = CD3DX12_RESOURCE_BARRIER::UAV(dest_as_buffer);
    //    _cmd_list->ResourceBarrier(1, &uav_barrier);
}

void phi::d3d12::command_list_translator::execute(const cmd::dispatch_rays& dispatch_rays)
{
    if (_bound.update_pso(dispatch_rays.pso))
    {
        _cmd_list->SetPipelineState1(_globals.pool_pipeline_states->getRaytrace(dispatch_rays.pso).raw_state_object);
    }


    D3D12_DISPATCH_RAYS_DESC desc = {};

    {
        auto const& table_info = _globals.pool_resources->getBufferInfo(dispatch_rays.table_raygen);
        auto const va = _globals.pool_resources->getRawResource(dispatch_rays.table_raygen)->GetGPUVirtualAddress();

        desc.RayGenerationShaderRecord.StartAddress = va;
        desc.RayGenerationShaderRecord.SizeInBytes = table_info.width;
    }

    {
        auto const& table_info = _globals.pool_resources->getBufferInfo(dispatch_rays.table_miss);
        auto const va = _globals.pool_resources->getRawResource(dispatch_rays.table_miss)->GetGPUVirtualAddress();

        desc.MissShaderTable.StartAddress = va;
        desc.MissShaderTable.SizeInBytes = table_info.width;
        desc.MissShaderTable.StrideInBytes = table_info.stride;
    }

    {
        auto const& table_info = _globals.pool_resources->getBufferInfo(dispatch_rays.table_hitgroups);
        auto const va = _globals.pool_resources->getRawResource(dispatch_rays.table_hitgroups)->GetGPUVirtualAddress();

        desc.HitGroupTable.StartAddress = va;
        desc.HitGroupTable.SizeInBytes = table_info.width;
        desc.HitGroupTable.StrideInBytes = table_info.stride;
    }

    desc.Width = dispatch_rays.width;
    desc.Height = dispatch_rays.height;
    desc.Depth = dispatch_rays.depth;

    _cmd_list->DispatchRays(&desc);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::clear_textures& clear_tex)
{
    resource_view_cpu_only const dynamic_rtvs = _thread_local.lin_alloc_rtvs.allocate(clear_tex.clear_ops.size());
    resource_view_cpu_only const dynamic_dsvs = _thread_local.lin_alloc_dsvs.allocate(clear_tex.clear_ops.size());

    for (uint8_t i = 0u; i < clear_tex.clear_ops.size(); ++i)
    {
        auto const& op = clear_tex.clear_ops[i];
        auto* const resource = _globals.pool_resources->getRawResource(op.rv.resource);

        if (is_depth_format(op.rv.pixel_format))
        {
            auto const dsv = dynamic_dsvs.get_index(i);

            // create the DSV on the fly
            auto const dsv_desc = util::create_dsv_desc(op.rv);
            _globals.device->CreateDepthStencilView(resource, &dsv_desc, dsv);

            _cmd_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, op.value.red_or_depth / 255.f,
                                             op.value.green_or_stencil, 0, nullptr);
        }
        else
        {
            auto const rtv = dynamic_rtvs.get_index(i);

            // create the RTV on the fly
            if (_globals.pool_resources->isBackbuffer(op.rv.resource))
            {
                // Create a default RTV for the backbuffer
                _globals.device->CreateRenderTargetView(resource, nullptr, rtv);
            }
            else
            {
                // Create an RTV based on the supplied info
                auto const rtv_desc = util::create_rtv_desc(op.rv);
                _globals.device->CreateRenderTargetView(resource, &rtv_desc, rtv);
            }

            float color_value[4] = {op.value.red_or_depth / 255.f, op.value.green_or_stencil / 255.f, op.value.blue / 255.f, op.value.alpha / 255.f};
            _cmd_list->ClearRenderTargetView(rtv, color_value, 0, nullptr);
        }
    }

    _thread_local.lin_alloc_rtvs.reset();
    _thread_local.lin_alloc_dsvs.reset();
}

void phi::d3d12::translator_thread_local_memory::initialize(ID3D12Device& device)
{
    lin_alloc_rtvs.initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, limits::max_render_targets);
    lin_alloc_dsvs.initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, limits::max_render_targets);
}
