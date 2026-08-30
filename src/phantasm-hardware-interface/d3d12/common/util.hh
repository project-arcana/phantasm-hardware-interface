#pragma once

#include <clean-core/capped_vector.hh>
#include <clean-core/macros.hh>

#include <typed-geometry/types/size.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/types.hh>

namespace phi::d3d12::util
{
/// returns a resource barrier description
/// with -1 in both mip_level and array_slice, all subresources are transitioned
/// with both specified, only a specific mip level and array slice is transitioned
/// (either both must be -1, or both must be specified. in the latter case, mip_size must be correct)
[[nodiscard]] D3D12_RESOURCE_BARRIER get_barrier_desc(
    ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, int mip_level = -1, int array_slice = -1, unsigned mip_size = 0);

[[nodiscard]] cc::capped_vector<D3D12_INPUT_ELEMENT_DESC, 16> get_native_vertex_format(cc::span<vertex_attribute_info const> attrib_info);

void set_object_name(ID3D12Object* object, char const* name, ...) CC_PRINTF_FUNC(2);

void set_object_name(IDXGIObject* object, char const* name, ...) CC_PRINTF_FUNC(2);

unsigned get_object_name(ID3D12Object* object, cc::span<char> out_name);

/// create a SRV description based on a shader_view_element
/// the raw resource is only required in case a raytracing AS is described
[[nodiscard]] D3D12_SHADER_RESOURCE_VIEW_DESC create_srv_desc(resource_view const& sve, D3D12_GPU_VIRTUAL_ADDRESS accelstruct_va);

/// create a UAV description based on a shader_view_element
[[nodiscard]] D3D12_UNORDERED_ACCESS_VIEW_DESC create_uav_desc(resource_view const& sve);

/// create a RTV description based on a shader_view_element
[[nodiscard]] D3D12_RENDER_TARGET_VIEW_DESC create_rtv_desc(resource_view const& sve);

/// create a DSV description based on a shader_view_element
[[nodiscard]] D3D12_DEPTH_STENCIL_VIEW_DESC create_dsv_desc(resource_view const& sve);

/// create a Sampler description based on a sampler config
[[nodiscard]] D3D12_SAMPLER_DESC create_sampler_desc(sampler_config const& config);

constexpr char const* to_queue_type_literal(D3D12_COMMAND_LIST_TYPE t)
{
    switch (t)
    {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        return "direct";
    case D3D12_COMMAND_LIST_TYPE_COPY:
        return "copy";
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        return "compute";
    default:
        return "unknown_type";
    }
}
}
