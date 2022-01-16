#include "cmd_list_translation.hh"

#ifdef PHI_HAS_OPTICK
#include <optick/optick.h>
#endif

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/command_reading.hh>
#include <phantasm-hardware-interface/common/format_size.hh>
#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/common/sse_hash.hh>

#include "common/diagnostic_util.hh"
#include "common/dxgi_format.hh"
#include "common/incomplete_state_cache.hh"
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

namespace
{
#ifdef PHI_HAS_OPTICK
Optick::GPUQueueType phiQueueTypeToOptickD3D12(phi::queue_type type)
{
    switch (type)
    {
    case phi::queue_type::direct:
    default:
        return Optick::GPU_QUEUE_GRAPHICS;
    case phi::queue_type::compute:
        return Optick::GPU_QUEUE_COMPUTE;
    case phi::queue_type::copy:
        return Optick::GPU_QUEUE_TRANSFER;
    }
}
#endif
} // namespace

void phi::d3d12::command_list_translator::initialize(
    ID3D12Device* device, ShaderViewPool* sv_pool, ResourcePool* resource_pool, PipelineStateObjectPool* pso_pool, AccelStructPool* as_pool, QueryPool* query_pool)
{
    _globals.initialize(device, sv_pool, resource_pool, pso_pool, as_pool, query_pool);
    _thread_local.initialize(*_globals.device);
}

void phi::d3d12::command_list_translator::destroy() { _thread_local.destroy(); }

void phi::d3d12::command_list_translator::translateCommandList(
    ID3D12GraphicsCommandList5* list, queue_type type, incomplete_state_cache* state_cache, std::byte const* buffer, size_t buffer_size)
{
    _cmd_list = list;
    _current_queue_type = type;
    _state_cache = state_cache;

    _bound.reset();
    _state_cache->reset();
    _last_code_location.reset();

    {
        command_stream_parser parser(buffer, buffer_size);
        command_stream_parser::iterator parserIterator = parser.begin();

        cmd::set_global_profile_scope const* cmdGlobalProfile = nullptr;
        if (parserIterator.has_cmds_left() && parserIterator.get_current_cmd_type() == phi::cmd::detail::cmd_type::set_global_profile_scope)
        {
            // if the very first command is set_global_profile_scope, use the provided event instead of the static one
            cmdGlobalProfile = static_cast<cmd::set_global_profile_scope const*>(parserIterator.get_current_cmd());
            parserIterator.skip_one_cmd();
        }


#ifdef PHI_HAS_OPTICK

        // start Optick context
        OPTICK_GPU_CONTEXT(_cmd_list, phiQueueTypeToOptickD3D12(_current_queue_type));

        // static default optick event if none is user supplied
        PHI_CREATE_OPTICK_EVENT(defaultOptickEvt, "PHI Command List");

        // use the set_global_profile_scope event if available
        Optick::EventDescription* globalOptickEvtDesc = defaultOptickEvt;
        if (cmdGlobalProfile && cmdGlobalProfile->optick_event)
        {
            globalOptickEvtDesc = cmdGlobalProfile->optick_event;
        }

        // start the optick GPU event and tag the buffer size
        Optick::EventData* const globalOptickEvt = Optick::GPUEvent::Start(*globalOptickEvtDesc);
        OPTICK_TAG("Size (Byte)", buffer_size);

        _current_optick_event_stack.clear();
#endif

        // bind the global descriptor heaps
        auto const gpu_heaps = _globals.pool_shader_views->getGPURelevantHeaps();
        _cmd_list->SetDescriptorHeaps(UINT(gpu_heaps.size()), gpu_heaps.data());

        // translate all contained commands
        while (parserIterator.has_cmds_left())
        {
            cmd::detail::dynamic_dispatch(*parserIterator.get_current_cmd(), *this);
            parserIterator.skip_one_cmd();
        }

#ifdef PHI_HAS_OPTICK
        // end last pending optick events
        while (!_current_optick_event_stack.empty())
        {
            Optick::GPUEvent::Stop(*_current_optick_event_stack.back());
            _current_optick_event_stack.pop_back();
        }

        // end the global optick event
        Optick::GPUEvent::Stop(*globalOptickEvt);
#endif
    }

    // close the list
    PHI_D3D12_VERIFY(_cmd_list->Close());

    // done
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::begin_render_pass& begin_rp)
{
    CC_ASSERT(_current_queue_type == queue_type::direct && "graphics commands are only valid on queue_type::direct");
    CC_ASSERT(begin_rp.viewport.width + begin_rp.viewport.height != 0 && "recording begin_render_pass with empty viewport");

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
    CC_ASSERT(draw.pipeline_state.is_valid() && "invalid PSO handle");

    auto const& pso_node = _globals.pool_pipeline_states->get(draw.pipeline_state);

    // PSO
    if (_bound.update_pso(draw.pipeline_state))
    {
        _cmd_list->SetPipelineState(pso_node.pPSO);
        _cmd_list->IASetPrimitiveTopology(pso_node.primitive_topology);
    }

    // Root signature
    if (_bound.update_root_sig(pso_node.pAssociatedRootSig->raw_root_sig))
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

    // Vertex buffers
    bind_vertex_buffers(draw.vertex_buffers);

    // Shader arguments
    {
        auto const& root_sig = *pso_node.pAssociatedRootSig;

        // root constants
        if (!root_sig.argument_maps.empty() && root_sig.argument_maps[0].root_const_param != unsigned(-1))
        {
            static_assert(sizeof(draw.root_constants) % sizeof(DWORD32) == 0, "root constant size not divisible by dword32 size");
            _cmd_list->SetGraphicsRoot32BitConstants(root_sig.argument_maps[0].root_const_param, sizeof(draw.root_constants) / sizeof(DWORD32),
                                                     draw.root_constants, 0);
        }

        CC_ASSERT(root_sig.argument_maps.size() == draw.shader_arguments.size() && "given amount of shader arguments deviates from pipeline state configuration");
        for (uint8_t i = 0; i < root_sig.argument_maps.size(); ++i)
        {
            auto& bound_arg = _bound.shader_args[i];
            auto const& arg = draw.shader_arguments[i];
            auto const& map = root_sig.argument_maps[i];

            if (map.cbv_param != uint32_t(-1))
            {
                CC_ASSERT(arg.constant_buffer.is_valid() && "argument CBV is missing");

                // Set the CBV / offset if it has changed
                if (bound_arg.update_cbv(arg.constant_buffer, arg.constant_buffer_offset) && arg.constant_buffer.is_valid())
                {
                    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(arg.constant_buffer, arg.constant_buffer_offset, 1) && "CBV offset OOB");

                    auto const cbv_va = _globals.pool_resources->getBufferInfo(arg.constant_buffer).gpu_va;
                    _cmd_list->SetGraphicsRootConstantBufferView(map.cbv_param, cbv_va + arg.constant_buffer_offset);
                }
            }

            // Set the shader view if it has changed
            if (bound_arg.update_shader_view(arg.shader_view))
            {
                if (map.srv_uav_table_param != uint32_t(-1))
                {
                    CC_ASSERT(_globals.pool_shader_views->hasSRVsUAVs(arg.shader_view) && "shader_view is missing SRVs/UAVs");
                    auto const sv_desc_table = _globals.pool_shader_views->getSRVUAVGPUHandle(arg.shader_view);
                    _cmd_list->SetGraphicsRootDescriptorTable(map.srv_uav_table_param, sv_desc_table);
                }

                if (map.sampler_table_param != uint32_t(-1))
                {
                    CC_ASSERT(_globals.pool_shader_views->hasSamplers(arg.shader_view) && "shader_view is missing Samplers");
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
        _cmd_list->DrawIndexedInstanced(draw.num_indices, draw.num_instances, draw.index_offset, draw.vertex_offset, 0);
    }
    else
    {
        _cmd_list->DrawInstanced(draw.num_indices, draw.num_instances, draw.index_offset, 0);
    }
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::draw_indirect& draw_indirect)
{
    CC_ASSERT(_current_queue_type == queue_type::direct && "graphics commands are only valid on queue_type::direct");
    auto const& pso_node = _globals.pool_pipeline_states->get(draw_indirect.pipeline_state);

    // PSO
    if (_bound.update_pso(draw_indirect.pipeline_state))
    {
        _cmd_list->SetPipelineState(pso_node.pPSO);
        _cmd_list->IASetPrimitiveTopology(pso_node.primitive_topology);
    }

    // Root signature
    if (_bound.update_root_sig(pso_node.pAssociatedRootSig->raw_root_sig))
    {
        _cmd_list->SetGraphicsRootSignature(_bound.raw_root_sig);
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

    // Vertex buffers
    bind_vertex_buffers(draw_indirect.vertex_buffers);

    // Shader arguments
    bool bPSOHasRootConsts = false;
    {
        auto const& root_sig = *pso_node.pAssociatedRootSig;

        // root constants
        if (!root_sig.argument_maps.empty() && root_sig.argument_maps[0].root_const_param != unsigned(-1))
        {
            bPSOHasRootConsts = true;
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
                    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(arg.constant_buffer, arg.constant_buffer_offset, 1) && "CBV offset OOB");

                    auto const cbv_va = _globals.pool_resources->getBufferInfo(arg.constant_buffer).gpu_va;
                    _cmd_list->SetGraphicsRootConstantBufferView(map.cbv_param, cbv_va + arg.constant_buffer_offset);
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


    uint32_t gpuCommandSizeBytes = 0;
    ID3D12CommandSignature* pComSig = nullptr;

    switch (draw_indirect.argument_type)
    {
    case indirect_command_type::draw:
        gpuCommandSizeBytes = sizeof(gpu_indirect_command_draw);
        pComSig = _globals.pool_pipeline_states->getGlobalComSigDraw();
        break;

    case indirect_command_type::draw_indexed:
        CC_ASSERT(draw_indirect.index_buffer.is_valid() && "Indirect drawing using type draw_indexed requires valid index buffer");

        gpuCommandSizeBytes = sizeof(gpu_indirect_command_draw_indexed);
        pComSig = _globals.pool_pipeline_states->getGlobalComSigDrawIndexed();
        break;

    case indirect_command_type::draw_indexed_with_id:
        CC_ASSERT(draw_indirect.index_buffer.is_valid() && "Indirect drawing using type draw_indexed_with_id requires valid index buffer");
        CC_ASSERT(bPSOHasRootConsts && "Indirect drawing using type draw_indexed_with_id requires enabled root constants on the PSO");
        CC_ASSERT(pso_node.pAssociatedComSigForDrawID != nullptr
                  && "Indirect drawing using type draw_indexed_with_id requires PSOs with enabled flag 'allow_draw_indirect_with_id' on creation");

        gpuCommandSizeBytes = sizeof(gpu_indirect_command_draw_indexed_with_id);
        pComSig = pso_node.pAssociatedComSigForDrawID;
        break;

    default:
        CC_UNREACHABLE("Invalid indirect command type");
        break;
    }

    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(draw_indirect.indirect_argument, draw_indirect.max_num_arguments * gpuCommandSizeBytes)
              && "indirect argument buffer accessed OOB on GPU");

    static_assert(sizeof(D3D12_DRAW_ARGUMENTS) == sizeof(gpu_indirect_command_draw), "gpu argument compiles to incorrect size");
    static_assert(sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) == sizeof(gpu_indirect_command_draw_indexed), "gpu argument compiles to incorrect size");
    static_assert(sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) + sizeof(DWORD) == sizeof(gpu_indirect_command_draw_indexed_with_id), "gpu argument compiles "
                                                                                                                             "to incorrect size");

    ID3D12Resource* const pArgumentBuffer = _globals.pool_resources->getRawResource(draw_indirect.indirect_argument);

    ID3D12Resource* const pCountBufferOrNull = _globals.pool_resources->getRawResourceOrNull(draw_indirect.count_buffer);

    _cmd_list->ExecuteIndirect(pComSig, draw_indirect.max_num_arguments,                      //
                               pArgumentBuffer, draw_indirect.indirect_argument.offset_bytes, //
                               pCountBufferOrNull, draw_indirect.count_buffer.offset_bytes    //
    );
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::dispatch& dispatch)
{
    auto const& pso_node = _globals.pool_pipeline_states->get(dispatch.pipeline_state);

    // PSO
    if (_bound.update_pso(dispatch.pipeline_state))
    {
        _cmd_list->SetPipelineState(pso_node.pPSO);
    }

    // Root signature
    if (_bound.update_root_sig(pso_node.pAssociatedRootSig->raw_root_sig))
    {
        _cmd_list->SetComputeRootSignature(_bound.raw_root_sig);
    }

    // Shader arguments
    {
        auto const& root_sig = *pso_node.pAssociatedRootSig;

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
                if (bound_arg.update_cbv(arg.constant_buffer, arg.constant_buffer_offset) && arg.constant_buffer.is_valid())
                {
                    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(arg.constant_buffer, arg.constant_buffer_offset, 1) && "CBV offset OOB");

                    auto const cbv_va = _globals.pool_resources->getBufferInfo(arg.constant_buffer).gpu_va;
                    _cmd_list->SetComputeRootConstantBufferView(map.cbv_param, cbv_va + arg.constant_buffer_offset);
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

void phi::d3d12::command_list_translator::execute(cmd::dispatch_indirect const& dispatch_indirect)
{
    auto const& pso_node = _globals.pool_pipeline_states->get(dispatch_indirect.pipeline_state);

    // PSO
    if (_bound.update_pso(dispatch_indirect.pipeline_state))
    {
        _cmd_list->SetPipelineState(pso_node.pPSO);
    }

    // Root signature
    if (_bound.update_root_sig(pso_node.pAssociatedRootSig->raw_root_sig))
    {
        _cmd_list->SetComputeRootSignature(_bound.raw_root_sig);
    }

    // Shader arguments
    {
        auto const& root_sig = *pso_node.pAssociatedRootSig;

        // root constants
        auto const root_constant_param = root_sig.argument_maps[0].root_const_param;
        if (root_constant_param != unsigned(-1))
        {
            static_assert(sizeof(dispatch_indirect.root_constants) % sizeof(DWORD32) == 0, "root constant size not divisible by dword32 size");
            _cmd_list->SetComputeRoot32BitConstants(root_constant_param, sizeof(dispatch_indirect.root_constants) / sizeof(DWORD32),
                                                    dispatch_indirect.root_constants, 0);
        }

        // regular shader arguments
        for (uint8_t i = 0; i < dispatch_indirect.shader_arguments.size(); ++i)
        {
            auto& bound_arg = _bound.shader_args[i];
            auto const& arg = dispatch_indirect.shader_arguments[i];
            auto const& map = root_sig.argument_maps[i];


            if (map.cbv_param != uint32_t(-1))
            {
                // Set the CBV / offset if it has changed
                if (bound_arg.update_cbv(arg.constant_buffer, arg.constant_buffer_offset) && arg.constant_buffer.is_valid())
                {
                    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(arg.constant_buffer, arg.constant_buffer_offset, 1) && "CBV offset OOB");

                    auto const cbv_va = _globals.pool_resources->getBufferInfo(arg.constant_buffer).gpu_va;
                    _cmd_list->SetComputeRootConstantBufferView(map.cbv_param, cbv_va + arg.constant_buffer_offset);
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
    auto const gpu_command_size_bytes = uint32_t(sizeof(gpu_indirect_command_dispatch));

    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(dispatch_indirect.argument_buffer_addr, dispatch_indirect.num_arguments * gpu_command_size_bytes)
              && "indirect argument buffer accessed OOB on GPU");

    ID3D12Resource* const raw_arg_buffer = _globals.pool_resources->getRawResource(dispatch_indirect.argument_buffer_addr);
    ID3D12CommandSignature* const comsig = _globals.pool_pipeline_states->getGlobalComSigDispatch();

    // NOTE: We use no count buffer, which makes the second argument determine the actual amount of args, not the max
    // NOTE: A global command sig is used, containing 256 dispatch arguments
    // the global comsig require no association with a rootsig making things a lot simpler
    // the amount of arguments configured in the rootsig is more or less arbitrary, could be increased possibly by a lot without cost
    CC_ASSERT(dispatch_indirect.num_arguments <= 256 && "Too many indirect arguments, contact maintainers");
    _cmd_list->ExecuteIndirect(comsig, dispatch_indirect.num_arguments, raw_arg_buffer, dispatch_indirect.argument_buffer_addr.offset_bytes, nullptr, 0);
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

    for (auto const& state_reset : transition_images.state_resets)
    {
        D3D12_RESOURCE_STATES const after = util::to_native(state_reset.new_state);
        D3D12_RESOURCE_STATES before;

        bool const before_known = _state_cache->transition_resource(state_reset.resource, after, before);
        CC_ASSERT(before_known && "state resets require a locally known before-state. transition the resources normally before using slice transitions");
    }
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::barrier_uav& barrier)
{
    cc::capped_vector<D3D12_RESOURCE_BARRIER, limits::max_uav_barriers> barriers;

    for (auto const res : barrier.resources)
    {
        auto const raw_res = _globals.pool_resources->getRawResource(res);

        D3D12_RESOURCE_BARRIER& desc = barriers.emplace_back();
        desc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        desc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        desc.UAV.pResource = raw_res;
    }

    if (barrier.resources.empty())
    {
        // full UAV barrier instead
        D3D12_RESOURCE_BARRIER& desc = barriers.emplace_back();
        desc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        desc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        desc.UAV.pResource = nullptr;
    }

    _cmd_list->ResourceBarrier(UINT(barriers.size()), barriers.data());
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::copy_buffer& copy_buf)
{
    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(copy_buf.source, copy_buf.num_bytes) && "copy_buffer source OOB");
    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(copy_buf.destination, copy_buf.num_bytes) && "copy_buffer dest OOB");

    _cmd_list->CopyBufferRegion(_globals.pool_resources->getRawResource(copy_buf.destination), copy_buf.destination.offset_bytes,
                                _globals.pool_resources->getRawResource(copy_buf.source), copy_buf.source.offset_bytes, copy_buf.num_bytes);
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
    auto const format_dxgi = util::to_dxgi_format(dest_info.pixel_format);

    D3D12_SUBRESOURCE_FOOTPRINT footprint;
    footprint.Format = format_dxgi;
    footprint.Width = copy_text.dest_width;
    footprint.Height = copy_text.dest_height;
    footprint.Depth = 1;
    // footprint.RowPitch:
    if (phi::util::is_block_compressed_format(dest_info.pixel_format))
    {
        // calculated differently for block-compressed textures
        unsigned const num_blocks = cc::int_div_ceil(copy_text.dest_width, 4u);
        auto const block_bytes = phi::util::get_block_format_4x4_size(dest_info.pixel_format);
        footprint.RowPitch = phi::util::align_up(num_blocks * block_bytes, 256);

        // width and height must be at least 4x4
        footprint.Width = cc::max(4u, footprint.Width);
        footprint.Height = cc::max(4u, footprint.Height);
    }
    else
    {
        auto const pixel_bytes = phi::util::get_format_size_bytes(dest_info.pixel_format);
        footprint.RowPitch = phi::util::align_up(pixel_bytes * copy_text.dest_width, 256);
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed_footprint;
    placed_footprint.Offset = copy_text.source.offset_bytes;
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
    footprint.Depth = copy_text.src_depth;
    // TODO: is this right for 3D textures?
    footprint.RowPitch = phi::util::align_up(phi::util::get_format_size_bytes(src_info.pixel_format) * footprint.Width, 256);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT dest_placed_footprint;
    dest_placed_footprint.Offset = copy_text.destination.offset_bytes;
    dest_placed_footprint.Footprint = footprint;

    auto const source_subres_index = copy_text.src_mip_index + copy_text.src_array_index * src_info.num_mips;

    CD3DX12_TEXTURE_COPY_LOCATION const source(_globals.pool_resources->getRawResource(copy_text.source), source_subres_index);
    CD3DX12_TEXTURE_COPY_LOCATION const dest(_globals.pool_resources->getRawResource(copy_text.destination), dest_placed_footprint);

    D3D12_BOX sourceBox;
    sourceBox.left = copy_text.src_offset_x;
    sourceBox.top = copy_text.src_offset_y;
    sourceBox.front = copy_text.src_offset_z;
    sourceBox.right = sourceBox.left + copy_text.src_width;
    sourceBox.bottom = sourceBox.top + copy_text.src_height;
    sourceBox.back = sourceBox.front + copy_text.src_depth;

#ifdef CC_ENABLE_ASSERTIONS
    auto const& srcDescFull = _globals.pool_resources->getTextureDescription(copy_text.source);
    CC_ASSERT((int)sourceBox.right <= srcDescFull.width && (int)sourceBox.bottom <= srcDescFull.height
              && (int)sourceBox.back <= srcDescFull.depth_or_array_size && "Source box out of bounds");
#endif

    _cmd_list->CopyTextureRegion(&dest, 0, 0, 0, &source, &sourceBox);
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

    CC_ASSERT(_globals.pool_resources->isBufferAccessInBounds(resolve.destination, resolve.num_queries * sizeof(UINT64))
              && "resolve query destination buffer accessed OOB");
    ID3D12Resource* const raw_dest_buffer = _globals.pool_resources->getRawResource(resolve.destination);
    _cmd_list->ResolveQueryData(heap, util::to_query_type(type), query_index_start, resolve.num_queries, raw_dest_buffer, resolve.destination.offset_bytes);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::begin_debug_label& label)
{
    //
    util::begin_pix_marker(_cmd_list, 0, label.string);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::end_debug_label&)
{
    //
    util::end_pix_marker(_cmd_list);
}

void phi::d3d12::command_list_translator::execute(cmd::begin_profile_scope const& scope)
{
#ifdef PHI_HAS_OPTICK
    if (_current_optick_event_stack.full())
    {
        PHI_LOG_WARN("Profile scopes are nested too deep, trace will be distorted");
        return;
    }

    if (scope.optick_event)
    {
        _current_optick_event_stack.push_back(Optick::GPUEvent::Start(*scope.optick_event));
    }
#endif
}

void phi::d3d12::command_list_translator::execute(cmd::end_profile_scope const&)
{
#ifdef PHI_HAS_OPTICK
    if (!_current_optick_event_stack.empty())
    {
        Optick::GPUEvent::Stop(*_current_optick_event_stack.back());
        _current_optick_event_stack.pop_back();
    }
#endif
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::update_bottom_level& blas_update)
{
    auto& dest_node = _globals.pool_accel_structs->getNode(blas_update.dest);

    auto const& dest_buffer = _globals.pool_resources->getBufferInfo(dest_node.buffer_as);
    ID3D12Resource* const dest_as_buffer = _globals.pool_resources->getRawResource(dest_node.buffer_as);

    // NOTE: this command is a strange CPU/GPU timeline hybrid - dest_node.geometries is required for both creation and this command,
    // we have to keep the data alive up until this point. DXR spec has this to say:
    //
    //     "The reason pGeometryDescs is a CPU based parameter as opposed to InstanceDescs which live on the GPU is,
    //     at least for initial implementations, the CPU needs to look at some of the information such as triangle
    //     counts in pGeometryDescs in order to schedule acceleration structure builds. Perhaps in the future more
    //     of the data can live on the GPU."
    //
    // figure out how much of the data is actually relevant for the build part, and maybe only request the real data in this command instead

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC as_create_info = {};
    as_create_info.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    as_create_info.Inputs.Flags = util::to_native_accel_struct_build_flags(dest_node.flags);
    as_create_info.Inputs.NumDescs = UINT(dest_node.geometries.size());

    as_create_info.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    as_create_info.Inputs.pGeometryDescs = dest_node.geometries.empty() ? nullptr : dest_node.geometries.data();

    as_create_info.DestAccelerationStructureData = dest_buffer.gpu_va;

    if (blas_update.scratch.is_valid())
    {
        as_create_info.ScratchAccelerationStructureData = _globals.pool_resources->getBufferInfo(blas_update.scratch).gpu_va;
    }
    else
    {
        CC_ASSERT(dest_node.buffer_scratch.is_valid() && "updates to acceleration structures created with no_internal_scratch_buffer require the scratch buffer field");
        as_create_info.ScratchAccelerationStructureData = _globals.pool_resources->getBufferInfo(dest_node.buffer_scratch).gpu_va;
    }

    if (blas_update.source.is_valid())
    {
        // there is a source - perform an update
        // note that src == dest is a valid case
        as_create_info.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

        auto& src_node = _globals.pool_accel_structs->getNode(blas_update.source);
        auto const src_va = _globals.pool_resources->getBufferInfo(src_node.buffer_as).gpu_va;
        as_create_info.SourceAccelerationStructureData = src_va;
    }

    _cmd_list->BuildRaytracingAccelerationStructure(&as_create_info, 0, nullptr);

    auto const uav_barrier = CD3DX12_RESOURCE_BARRIER::UAV(dest_as_buffer);
    _cmd_list->ResourceBarrier(1, &uav_barrier);
}

void phi::d3d12::command_list_translator::execute(const phi::cmd::update_top_level& tlas_update)
{
    auto& dest_node = _globals.pool_accel_structs->getNode(tlas_update.dest_accel_struct);
    // ID3D12Resource* const dest_as_buffer = _globals.pool_resources->getRawResource(dest_node.buffer_as);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC as_create_info = {};
    as_create_info.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    as_create_info.Inputs.Flags = util::to_native_accel_struct_build_flags(dest_node.flags);
    as_create_info.Inputs.NumDescs = tlas_update.num_instances;

    as_create_info.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    as_create_info.Inputs.InstanceDescs
        = _globals.pool_resources->getBufferInfo(tlas_update.source_instances_addr.buffer).gpu_va + tlas_update.source_instances_addr.offset_bytes;

    as_create_info.DestAccelerationStructureData = dest_node.buffer_as_va;

    if (tlas_update.scratch.is_valid())
    {
        as_create_info.ScratchAccelerationStructureData = _globals.pool_resources->getBufferInfo(tlas_update.scratch).gpu_va;
    }
    else
    {
        CC_ASSERT(dest_node.buffer_scratch.is_valid() && "updates to acceleration structures created with no_internal_scratch_buffer require the scratch buffer field");
        as_create_info.ScratchAccelerationStructureData = _globals.pool_resources->getBufferInfo(dest_node.buffer_scratch).gpu_va;
    }

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
        auto const table_va = _globals.pool_resources->getBufferInfo(dispatch_rays.table_ray_generation.buffer).gpu_va;

        desc.RayGenerationShaderRecord.StartAddress = table_va + dispatch_rays.table_ray_generation.offset_bytes;
        desc.RayGenerationShaderRecord.SizeInBytes = dispatch_rays.table_ray_generation.size_bytes;

        CC_ASSERT(phi::util::is_aligned(desc.RayGenerationShaderRecord.StartAddress, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT)
                  && "ray generation shader table buffer offset is not aligned to 64B");
    }

    auto const f_fill_out_buffer_range = [&](buffer_range_and_stride const& in_range, D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE& out_range)
    {
        if (!in_range.buffer.is_valid())
            return;

        auto const buffer_va = _globals.pool_resources->getBufferInfo(in_range.buffer).gpu_va;

        out_range.StartAddress = buffer_va + in_range.offset_bytes;
        out_range.SizeInBytes = in_range.size_bytes;
        out_range.StrideInBytes = in_range.stride_bytes;

        CC_ASSERT(phi::util::is_aligned(out_range.StartAddress, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT) && "shader table buffer offset is not aligned to 64B");
        CC_ASSERT(out_range.StrideInBytes > 0 ? phi::util::is_aligned(out_range.StrideInBytes, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT)
                                              : 1 && "shader table stride is not aligned to 32B");
    };

    f_fill_out_buffer_range(dispatch_rays.table_miss, desc.MissShaderTable);
    f_fill_out_buffer_range(dispatch_rays.table_hit_groups, desc.HitGroupTable);
    f_fill_out_buffer_range(dispatch_rays.table_callable, desc.CallableShaderTable);

    desc.Width = dispatch_rays.dispatch_x;
    desc.Height = dispatch_rays.dispatch_y;
    desc.Depth = dispatch_rays.dispatch_z;

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

        if (phi::util::is_depth_format(op.rv.texture_info.pixel_format))
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

void phi::d3d12::command_list_translator::execute(cmd::code_location_marker const& marker)
{
    _last_code_location.file = marker.file;
    _last_code_location.function = marker.function;
    _last_code_location.line = marker.line;
}

void phi::d3d12::command_list_translator::execute(cmd::set_global_profile_scope const&)
{
    // do nothing
}

void phi::d3d12::command_list_translator::bind_vertex_buffers(handle::resource const vertex_buffers[limits::max_vertex_buffers])
{
    uint64_t const vert_hash = phi::util::sse_hash_type<handle::resource>(vertex_buffers, limits::max_vertex_buffers);
    if (vert_hash != _bound.vertex_buffer_hash)
    {
        _bound.vertex_buffer_hash = vert_hash;
        if (vertex_buffers[0].is_valid())
        {
            D3D12_VERTEX_BUFFER_VIEW vbvs[limits::max_vertex_buffers];
            uint32_t numVertexBuffers = 0;

            for (auto i = 0u; i < limits::max_vertex_buffers; ++i)
            {
                if (!vertex_buffers[i].is_valid())
                    break;

                vbvs[i] = _globals.pool_resources->getVertexBufferView(vertex_buffers[i]);
                ++numVertexBuffers;
            }

            _cmd_list->IASetVertexBuffers(0, numVertexBuffers, vbvs);
        }
    }
}

void phi::d3d12::translator_thread_local_memory::initialize(ID3D12Device& device)
{
    lin_alloc_rtvs.initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, limits::max_render_targets);
    lin_alloc_dsvs.initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, limits::max_render_targets);
}

void phi::d3d12::translator_thread_local_memory::destroy()
{
    lin_alloc_rtvs.destroy();
    lin_alloc_dsvs.destroy();
}
