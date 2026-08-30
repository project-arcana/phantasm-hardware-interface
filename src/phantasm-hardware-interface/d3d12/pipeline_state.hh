#pragma once

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/arguments.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>

namespace phi::d3d12
{
// creates a (classical) graphics pipeline state
[[nodiscard]] ID3D12PipelineState* create_pipeline_state(ID3D12Device5& device,
                                                         ID3D12RootSignature* root_sig,
                                                         cc::span<D3D12_INPUT_ELEMENT_DESC const> vertex_input_layout,
                                                         const arg::framebuffer_config& framebuffer_format,
                                                         arg::graphics_shaders shader_stages,
                                                         arg::pipeline_config const& config,
                                                         primitive_topology topology);

// creates a mesh shading pipeline state
// shader stages must contain at least a mesh shader, and optionally an amplification and a pixel shader
// NOTE: pipeline_config::topology is ignored
[[nodiscard]] ID3D12PipelineState* create_mesh_pipeline_state(ID3D12Device5& device,
                                                              ID3D12RootSignature* root_sig,
                                                              const arg::framebuffer_config& framebuffer_format,
                                                              arg::graphics_shaders shader_stages,
                                                              arg::pipeline_config const& config);

// creates a compute pipeline state
[[nodiscard]] ID3D12PipelineState* create_compute_pipeline_state(ID3D12Device5& device, ID3D12RootSignature* root_sig, arg::shader_binary const& shader);
} // namespace phi::d3d12
