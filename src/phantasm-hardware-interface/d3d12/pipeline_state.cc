#include "pipeline_state.hh"

#include <phantasm-hardware-interface/d3d12/common/d3dx12.hh>
#include <phantasm-hardware-interface/d3d12/common/dxgi_format.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

ID3D12PipelineState* phi::d3d12::create_pipeline_state(ID3D12Device& device,
                                                               ID3D12RootSignature* root_sig,
                                                               cc::span<const D3D12_INPUT_ELEMENT_DESC> vertex_input_layout,
                                                               phi::arg::framebuffer_config const& framebuffer_format,
                                                               phi::arg::graphics_shader_stages shader_stages,
                                                               const phi::primitive_pipeline_config& config)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = {!vertex_input_layout.empty() ? vertex_input_layout.data() : nullptr, UINT(vertex_input_layout.size())};
    pso_desc.pRootSignature = root_sig;

    constexpr auto const to_bytecode = [](arg::shader_stage const& s) { return D3D12_SHADER_BYTECODE{s.binary.data, s.binary.size}; };

    for (arg::shader_stage const& s : shader_stages)
    {
        switch (s.domain)
        {
        case shader_domain::pixel:
            pso_desc.PS = to_bytecode(s);
            break;
        case shader_domain::vertex:
            pso_desc.VS = to_bytecode(s);
            break;
        case shader_domain::domain:
            pso_desc.DS = to_bytecode(s);
            break;
        case shader_domain::hull:
            pso_desc.HS = to_bytecode(s);
            break;
        case shader_domain::geometry:
            pso_desc.GS = to_bytecode(s);
            break;
        default:
            break;
        }
    }


    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_desc.RasterizerState.CullMode = util::to_native(config.cull);
    pso_desc.RasterizerState.FrontCounterClockwise = true;

    pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso_desc.DepthStencilState.DepthEnable = config.depth != phi::depth_function::none && !framebuffer_format.depth_target.empty();
    pso_desc.DepthStencilState.DepthFunc = util::to_native(config.depth);
    pso_desc.DepthStencilState.DepthWriteMask = config.depth_readonly ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;

    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = util::to_native(config.topology);

    pso_desc.NumRenderTargets = UINT(framebuffer_format.render_targets.size());
    pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    for (auto i = 0u; i < framebuffer_format.render_targets.size(); ++i)
    {
        auto const& rt = framebuffer_format.render_targets[i];
        pso_desc.RTVFormats[i] = util::to_dxgi_format(rt.format);
        pso_desc.BlendState.IndependentBlendEnable = pso_desc.BlendState.IndependentBlendEnable || rt.blend_enable;

        if (rt.blend_enable)
        {
            auto& blend_state = pso_desc.BlendState.RenderTarget[i];

            blend_state.LogicOpEnable = framebuffer_format.logic_op_enable;
            blend_state.LogicOp = util::to_native(framebuffer_format.logic_op);

            blend_state.BlendEnable = true;
            blend_state.BlendOp = util::to_native(rt.blend_op_color);
            blend_state.SrcBlend = util::to_native(rt.blend_color_src);
            blend_state.DestBlend = util::to_native(rt.blend_color_dest);
            blend_state.BlendOpAlpha = util::to_native(rt.blend_op_alpha);
            blend_state.SrcBlendAlpha = util::to_native(rt.blend_alpha_src);
            blend_state.DestBlendAlpha = util::to_native(rt.blend_alpha_dest);
        }
    }

    pso_desc.DSVFormat = pso_desc.DepthStencilState.DepthEnable ? util::to_dxgi_format(framebuffer_format.depth_target[0]) : DXGI_FORMAT_UNKNOWN;

    pso_desc.SampleDesc.Count = UINT(config.samples);
    pso_desc.SampleDesc.Quality = config.samples != 1 ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0;
    pso_desc.NodeMask = 0;
    pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    ID3D12PipelineState* pso;
    PHI_D3D12_VERIFY(device.CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)));
    return pso;
}

ID3D12PipelineState* phi::d3d12::create_compute_pipeline_state(ID3D12Device& device, ID3D12RootSignature* root_sig, std::byte const* binary_data, size_t binary_size)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = root_sig;
    pso_desc.CS = D3D12_SHADER_BYTECODE{binary_data, binary_size};

    ID3D12PipelineState* pso;
    PHI_D3D12_VERIFY(device.CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso)));
    return pso;
}
