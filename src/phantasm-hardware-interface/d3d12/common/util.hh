#pragma once

#include <typed-geometry/types/size.hh>

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/assets/vertex_attrib_info.hh>
#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>

namespace phi::d3d12::util
{
inline void set_viewport(ID3D12GraphicsCommandList* command_list, tg::isize2 size)
{
    auto const viewport = D3D12_VIEWPORT{0.f, 0.f, float(size.width), float(size.height), 0.f, 1.f};
    auto const scissor_rect = D3D12_RECT{0, 0, LONG(size.width), LONG(size.height)};

    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor_rect);
}

/// returns a resource barrier description
/// with -1 in both mip_level and array_slice, all subresources are transitioned
/// with both specified, only a specific mip level and array slice is transitioned
/// (either both must be -1, or both must be specified. in the latter case, mip_size must be correct)
[[nodiscard]] D3D12_RESOURCE_BARRIER get_barrier_desc(
    ID3D12Resource* res, resource_state before, resource_state after, int mip_level = -1, int array_slice = -1, unsigned mip_size = 0);

[[nodiscard]] cc::capped_vector<D3D12_INPUT_ELEMENT_DESC, 16> get_native_vertex_format(cc::span<vertex_attribute_info const> attrib_info);

void set_object_name(ID3D12Object* object, char const* name, ...);

/// create a SRV description based on a shader_view_element
/// the raw resource is only required in case a raytracing AS is described
[[nodiscard]] D3D12_SHADER_RESOURCE_VIEW_DESC create_srv_desc(shader_view_element const& sve, ID3D12Resource* raw_resource);

/// create a UAV description based on a shader_view_element
[[nodiscard]] D3D12_UNORDERED_ACCESS_VIEW_DESC create_uav_desc(shader_view_element const& sve);

/// create a RTV description based on a shader_view_element
[[nodiscard]] D3D12_RENDER_TARGET_VIEW_DESC create_rtv_desc(shader_view_element const& sve);

/// create a DSV description based on a shader_view_element
[[nodiscard]] D3D12_DEPTH_STENCIL_VIEW_DESC create_dsv_desc(shader_view_element const& sve);

/// create a Sampler description based on a sampler config
[[nodiscard]] D3D12_SAMPLER_DESC create_sampler_desc(sampler_config const& config);
}
