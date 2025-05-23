#include "pipeline_state.hh"

#ifdef PHI_HAS_OPTICK
#include <optick.h>
#endif

#include <phantasm-hardware-interface/d3d12/common/d3dx12.hh>
#include <phantasm-hardware-interface/d3d12/common/dxgi_format.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

namespace phi::d3d12
{
namespace
{
void PopulateRasterizerState(D3D12_RASTERIZER_DESC* pDest, phi::arg::pipeline_config const& config)
{
    D3D12_RASTERIZER_DESC& Dest = *pDest;
    Dest = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    Dest.CullMode = util::to_native(config.cull);
    Dest.FillMode = config.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    Dest.FrontCounterClockwise = config.frontface_counterclockwise;
    Dest.ConservativeRaster = config.conservative_raster ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    Dest.DepthBias = config.depth_bias;
    Dest.SlopeScaledDepthBias = config.slope_scaled_depth_bias;
}

void PopulateDepthStencilState(D3D12_DEPTH_STENCIL_DESC* pDest, phi::arg::pipeline_config const& config, phi::arg::framebuffer_config const& framebuffer_format)
{
    D3D12_DEPTH_STENCIL_DESC& Dest = *pDest;
    Dest = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    Dest.DepthEnable = config.depth != phi::depth_function::none && framebuffer_format.depth_target != format::none;
    Dest.DepthFunc = util::to_native(config.depth);
    Dest.DepthWriteMask = config.depth_readonly ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
}

void PopulateBlendState(D3D12_BLEND_DESC* pDest, phi::arg::framebuffer_config const& framebuffer_format)
{
    D3D12_BLEND_DESC& Dest = *pDest;

    Dest = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    for (auto i = 0u; i < framebuffer_format.render_targets.size(); ++i)
    {
        auto const& rt = framebuffer_format.render_targets[i];
        Dest.IndependentBlendEnable = Dest.IndependentBlendEnable || rt.blend_enable;

        if (rt.blend_enable)
        {
            auto& blend_state = Dest.RenderTarget[i];

            blend_state.LogicOpEnable = framebuffer_format.logic_op_enable;
            blend_state.LogicOp = util::to_native(framebuffer_format.logic_op);

            blend_state.BlendEnable = true;
            blend_state.BlendOp = util::to_native(rt.state.blend_op_color);
            blend_state.SrcBlend = util::to_native(rt.state.blend_color_src);
            blend_state.DestBlend = util::to_native(rt.state.blend_color_dest);
            blend_state.BlendOpAlpha = util::to_native(rt.state.blend_op_alpha);
            blend_state.SrcBlendAlpha = util::to_native(rt.state.blend_alpha_src);
            blend_state.DestBlendAlpha = util::to_native(rt.state.blend_alpha_dest);
        }
    }
}

D3D12_SHADER_BYTECODE GetShaderBytecode(arg::shader_binary const& binary) { return D3D12_SHADER_BYTECODE{binary.data, binary.size}; }
}
}

ID3D12PipelineState* phi::d3d12::create_pipeline_state(ID3D12Device5& device,
                                                       ID3D12RootSignature* root_sig,
                                                       cc::span<const D3D12_INPUT_ELEMENT_DESC> vertex_input_layout,
                                                       phi::arg::framebuffer_config const& framebuffer_format,
                                                       phi::arg::graphics_shaders shader_stages,
                                                       const phi::arg::pipeline_config& config)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = {!vertex_input_layout.empty() ? vertex_input_layout.data() : nullptr, UINT(vertex_input_layout.size())};
    pso_desc.pRootSignature = root_sig;

    for (arg::graphics_shader const& s : shader_stages)
    {
        switch (s.stage)
        {
        case shader_stage::pixel:
            pso_desc.PS = GetShaderBytecode(s.binary);
            break;
        case shader_stage::vertex:
            pso_desc.VS = GetShaderBytecode(s.binary);
            break;
        case shader_stage::domain:
            pso_desc.DS = GetShaderBytecode(s.binary);
            break;
        case shader_stage::hull:
            pso_desc.HS = GetShaderBytecode(s.binary);
            break;
        case shader_stage::geometry:
            pso_desc.GS = GetShaderBytecode(s.binary);
            break;
        default:
            CC_ASSERT(false && "invalid shader stage for a graphics pipeline state");
            break;
        }
    }

    // this is not really a requirement
    // CC_ASSERT(framebuffer_format.render_targets.empty() ? true : pso_desc.PS.pShaderBytecode != nullptr && "creating a PSO with rendertargets, but missing pixel shader");

    PopulateRasterizerState(&pso_desc.RasterizerState, config);
    PopulateDepthStencilState(&pso_desc.DepthStencilState, config, framebuffer_format);
    PopulateBlendState(&pso_desc.BlendState, framebuffer_format);

    pso_desc.PrimitiveTopologyType = util::to_native(config.topology);

    pso_desc.NumRenderTargets = UINT(framebuffer_format.render_targets.size());

    for (auto i = 0u; i < framebuffer_format.render_targets.size(); ++i)
    {
        auto const& rt = framebuffer_format.render_targets[i];
        pso_desc.RTVFormats[i] = util::to_dxgi_format(rt.fmt);
    }

    pso_desc.DSVFormat = pso_desc.DepthStencilState.DepthEnable ? util::to_dxgi_format(framebuffer_format.depth_target) : DXGI_FORMAT_UNKNOWN;

    pso_desc.SampleMask = UINT_MAX;
    pso_desc.SampleDesc.Count = UINT(config.samples);
    pso_desc.SampleDesc.Quality = config.samples != 1 ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0;
    pso_desc.NodeMask = 0;
    pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    ID3D12PipelineState* pso = nullptr;

#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT("ID3D12Device::CreateGraphicsPipelineState");
#endif
    HRESULT const hres = device.CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    return pso;
}

ID3D12PipelineState* phi::d3d12::create_mesh_pipeline_state(ID3D12Device5& device,
                                                            ID3D12RootSignature* root_sig,
                                                            const arg::framebuffer_config& framebuffer_format,
                                                            arg::graphics_shaders shader_stages,
                                                            arg::pipeline_config const& config)
{
    struct PSO_STREAM
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_AS AS;
        CD3DX12_PIPELINE_STATE_STREAM_MS MS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RenderTargets;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendState;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK SampleMask;
    } Stream = {};

    Stream.pRootSignature = root_sig;

    for (arg::graphics_shader const& s : shader_stages)
    {
        switch (s.stage)
        {
        case shader_stage::pixel:
            Stream.PS = GetShaderBytecode(s.binary);
            break;
        case shader_stage::amplification:
            Stream.AS = GetShaderBytecode(s.binary);
            break;
        case shader_stage::mesh:
            Stream.MS = GetShaderBytecode(s.binary);
            break;
        default:
            CC_ASSERT(false && "invalid shader stage for a mesh pipeline state");
            break;
        }
    }
    // it's not required to drop either the STREAM_PS or STREAM_AS structs if no shader is provided.
    // the null arguments will be ignored
    // see: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_pipeline_state_stream_desc

    PopulateRasterizerState(&Stream.RasterizerState, config);
    PopulateDepthStencilState(&Stream.DepthStencilState, config, framebuffer_format);
    PopulateBlendState(&Stream.BlendState, framebuffer_format);

    D3D12_RT_FORMAT_ARRAY& RenderTargets = Stream.RenderTargets;
    RenderTargets.NumRenderTargets = UINT(framebuffer_format.render_targets.size());

    for (auto i = 0u; i < framebuffer_format.render_targets.size(); ++i)
    {
        auto const& rt = framebuffer_format.render_targets[i];
        RenderTargets.RTFormats[i] = util::to_dxgi_format(rt.fmt);
    }

    Stream.DSVFormat = Stream.DepthStencilState.operator const CD3DX12_DEPTH_STENCIL_DESC&().DepthEnable ? util::to_dxgi_format(framebuffer_format.depth_target)
                                                                                                         : DXGI_FORMAT_UNKNOWN;

    Stream.SampleMask = UINT_MAX;
    DXGI_SAMPLE_DESC& SampleDesc = Stream.SampleDesc;
    SampleDesc.Count = UINT(config.samples);
    SampleDesc.Quality = config.samples != 1 ? DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN : 0;

    D3D12_PIPELINE_STATE_STREAM_DESC pso_desc = {};
    pso_desc.SizeInBytes = sizeof(Stream);
    pso_desc.pPipelineStateSubobjectStream = &Stream;

    ID3D12PipelineState* pso = nullptr;

#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT("ID3D12Device2::CreatePipelineState");
#endif
    HRESULT const hres = device.CreatePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    return pso;
}

ID3D12PipelineState* phi::d3d12::create_compute_pipeline_state(ID3D12Device5& device, ID3D12RootSignature* root_sig, arg::shader_binary const& shader)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = root_sig;
    pso_desc.CS = GetShaderBytecode(shader);
    pso_desc.NodeMask = 0;
    pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    ID3D12PipelineState* pso = nullptr;

#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT("ID3D12Device::CreateComputePipelineState");
#endif
    HRESULT const hres = device.CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
    return pso;
}
