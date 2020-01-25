#pragma once

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/arguments.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>

namespace phi::d3d12
{
[[nodiscard]] ID3D12PipelineState* create_pipeline_state(ID3D12Device& device,
                                                         ID3D12RootSignature* root_sig,
                                                         cc::span<D3D12_INPUT_ELEMENT_DESC const> vertex_input_layout,
                                                         const arg::framebuffer_config& framebuffer_format,
                                                         arg::graphics_shaders shader_stages,
                                                         graphics_pipeline_config const& config);

[[nodiscard]] ID3D12PipelineState* create_compute_pipeline_state(ID3D12Device& device, ID3D12RootSignature* root_sig, const std::byte* binary_data, size_t binary_size);
}
