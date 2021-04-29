#pragma once

#include <phantasm-hardware-interface/types.hh>

#include <clean-core/assert.hh>

#include "d3d12_sanitized.hh"

namespace phi::d3d12::util
{
[[nodiscard]] constexpr D3D12_RESOURCE_STATES to_native(resource_state state)
{
    using rs = resource_state;
    switch (state)
    {
    case rs::undefined:
    case rs::unknown:
        return D3D12_RESOURCE_STATE_COMMON;

    case rs::vertex_buffer:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case rs::index_buffer:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;

    case rs::constant_buffer:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case rs::shader_resource:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case rs::shader_resource_nonpixel:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case rs::unordered_access:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    case rs::render_target:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case rs::depth_read:
        return D3D12_RESOURCE_STATE_DEPTH_READ;
    case rs::depth_write:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;

    case rs::indirect_argument:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

    case rs::copy_src:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case rs::copy_dest:
        return D3D12_RESOURCE_STATE_COPY_DEST;

    case rs::resolve_src:
        return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    case rs::resolve_dest:
        return D3D12_RESOURCE_STATE_RESOLVE_DEST;

    case rs::present:
        return D3D12_RESOURCE_STATE_PRESENT;

    case rs::raytrace_accel_struct:
        return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    }


    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_RESOURCE_STATE_COMMON;
}

[[nodiscard]] constexpr D3D12_HEAP_TYPE to_native(phi::resource_heap type)
{
    switch (type)
    {
    case phi::resource_heap::gpu:
        return D3D12_HEAP_TYPE_DEFAULT;
    case phi::resource_heap::upload:
        return D3D12_HEAP_TYPE_UPLOAD;
    case phi::resource_heap::readback:
        return D3D12_HEAP_TYPE_READBACK;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_HEAP_TYPE_DEFAULT;
}

[[nodiscard]] constexpr D3D12_PRIMITIVE_TOPOLOGY_TYPE to_native(phi::primitive_topology topology)
{
    switch (topology)
    {
    case phi::primitive_topology::triangles:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case phi::primitive_topology::lines:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case phi::primitive_topology::points:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case phi::primitive_topology::patches:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

[[nodiscard]] constexpr D3D12_PRIMITIVE_TOPOLOGY to_native_topology(phi::primitive_topology topology)
{
    switch (topology)
    {
    case phi::primitive_topology::triangles:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case phi::primitive_topology::lines:
        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case phi::primitive_topology::points:
        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case phi::primitive_topology::patches:
        return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST; // TODO
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

[[nodiscard]] constexpr D3D12_COMPARISON_FUNC to_native(phi::depth_function depth_func)
{
    switch (depth_func)
    {
    case phi::depth_function::none:
        return D3D12_COMPARISON_FUNC_LESS; // sane defaults
    case phi::depth_function::less:
        return D3D12_COMPARISON_FUNC_LESS;
    case phi::depth_function::less_equal:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case phi::depth_function::greater:
        return D3D12_COMPARISON_FUNC_GREATER;
    case phi::depth_function::greater_equal:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case phi::depth_function::equal:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case phi::depth_function::not_equal:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case phi::depth_function::always:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    case phi::depth_function::never:
        return D3D12_COMPARISON_FUNC_NEVER;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_COMPARISON_FUNC_LESS;
}

[[nodiscard]] constexpr D3D12_CULL_MODE to_native(phi::cull_mode cull_mode)
{
    switch (cull_mode)
    {
    case phi::cull_mode::none:
        return D3D12_CULL_MODE_NONE;
    case phi::cull_mode::back:
        return D3D12_CULL_MODE_BACK;
    case phi::cull_mode::front:
        return D3D12_CULL_MODE_FRONT;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_CULL_MODE_NONE;
}

[[nodiscard]] constexpr D3D12_COMMAND_LIST_TYPE to_native(queue_type type)
{
    switch (type)
    {
    case queue_type::direct:
        return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case queue_type::copy:
        return D3D12_COMMAND_LIST_TYPE_COPY;
    case queue_type::compute:
        return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_COMMAND_LIST_TYPE_DIRECT;
}

[[nodiscard]] D3D12_SRV_DIMENSION to_native_srv_dim(resource_view_dimension sv_dim)
{
    switch (sv_dim)
    {
    case resource_view_dimension::buffer:
        return D3D12_SRV_DIMENSION_BUFFER;
    case resource_view_dimension::texture1d:
        return D3D12_SRV_DIMENSION_TEXTURE1D;
    case resource_view_dimension::texture1d_array:
        return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
    case resource_view_dimension::texture2d:
        return D3D12_SRV_DIMENSION_TEXTURE2D;
    case resource_view_dimension::texture2d_ms:
        return D3D12_SRV_DIMENSION_TEXTURE2DMS;
    case resource_view_dimension::texture2d_array:
        return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    case resource_view_dimension::texture2d_ms_array:
        return D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
    case resource_view_dimension::texture3d:
        return D3D12_SRV_DIMENSION_TEXTURE3D;
    case resource_view_dimension::texturecube:
        return D3D12_SRV_DIMENSION_TEXTURECUBE;
    case resource_view_dimension::texturecube_array:
        return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
    case resource_view_dimension::raytracing_accel_struct:
        return D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    default:
        CC_UNREACHABLE("to_native_srv_dim uncaught argument");
        return D3D12_SRV_DIMENSION_UNKNOWN;
    }
}

[[nodiscard]] constexpr D3D12_UAV_DIMENSION to_native_uav_dim(resource_view_dimension sv_dim)
{
    switch (sv_dim)
    {
    case resource_view_dimension::buffer:
        return D3D12_UAV_DIMENSION_BUFFER;

    case resource_view_dimension::texture1d:
        return D3D12_UAV_DIMENSION_TEXTURE1D;

    case resource_view_dimension::texture1d_array:
        return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;

    case resource_view_dimension::texture2d:
        return D3D12_UAV_DIMENSION_TEXTURE2D;

    case resource_view_dimension::texture2d_array:
    case resource_view_dimension::texturecube:
        return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;

    case resource_view_dimension::texture3d:
        return D3D12_UAV_DIMENSION_TEXTURE3D;

    default:
        return D3D12_UAV_DIMENSION_UNKNOWN;
    }
}

[[nodiscard]] constexpr bool is_valid_as_uav_dim(resource_view_dimension sv_dim) { return to_native_uav_dim(sv_dim) != D3D12_UAV_DIMENSION_UNKNOWN; }

[[nodiscard]] constexpr D3D12_RTV_DIMENSION to_native_rtv_dim(resource_view_dimension sv_dim)
{
    switch (sv_dim)
    {
    case resource_view_dimension::buffer:
        return D3D12_RTV_DIMENSION_BUFFER;
    case resource_view_dimension::texture1d:
        return D3D12_RTV_DIMENSION_TEXTURE1D;
    case resource_view_dimension::texture1d_array:
        return D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
    case resource_view_dimension::texture2d:
        return D3D12_RTV_DIMENSION_TEXTURE2D;
    case resource_view_dimension::texture2d_ms:
        return D3D12_RTV_DIMENSION_TEXTURE2DMS;
    case resource_view_dimension::texture2d_array:
        return D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    case resource_view_dimension::texture2d_ms_array:
        return D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
    case resource_view_dimension::texture3d:
        return D3D12_RTV_DIMENSION_TEXTURE3D;
    default:

        CC_UNREACHABLE("to_native uncaught argument");
        return D3D12_RTV_DIMENSION_UNKNOWN;
    }
}

[[nodiscard]] constexpr bool is_valid_as_rtv_dim(resource_view_dimension sv_dim) { return to_native_rtv_dim(sv_dim) != D3D12_RTV_DIMENSION_UNKNOWN; }

[[nodiscard]] constexpr D3D12_DSV_DIMENSION to_native_dsv_dim(resource_view_dimension sv_dim)
{
    switch (sv_dim)
    {
    case resource_view_dimension::texture1d:
        return D3D12_DSV_DIMENSION_TEXTURE1D;
    case resource_view_dimension::texture1d_array:
        return D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
    case resource_view_dimension::texture2d:
        return D3D12_DSV_DIMENSION_TEXTURE2D;
    case resource_view_dimension::texture2d_ms:
        return D3D12_DSV_DIMENSION_TEXTURE2DMS;
    case resource_view_dimension::texture2d_array:
        return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    case resource_view_dimension::texture2d_ms_array:
        return D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
    default:

        CC_UNREACHABLE("to_native uncaught argument");
        return D3D12_DSV_DIMENSION_UNKNOWN;
    }
}

[[nodiscard]] constexpr D3D12_FILTER to_native(sampler_filter filter, bool with_compare)
{
    if (with_compare)
    {
        switch (filter)
        {
        case sampler_filter::min_mag_mip_point:
            return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        case sampler_filter::min_point_mag_linear_mip_point:
            return D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT;
        case sampler_filter::min_linear_mag_mip_point:
            return D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT;
        case sampler_filter::min_mag_linear_mip_point:
            return D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        case sampler_filter::min_point_mag_mip_linear:
            return D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR;
        case sampler_filter::min_linear_mag_point_mip_linear:
            return D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        case sampler_filter::min_mag_point_mip_linear:
            return D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR;
        case sampler_filter::min_mag_mip_linear:
            return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        case sampler_filter::anisotropic:
            return D3D12_FILTER_COMPARISON_ANISOTROPIC;
        }
    }
    else
    {
        switch (filter)
        {
        case sampler_filter::min_mag_mip_point:
            return D3D12_FILTER_MIN_MAG_MIP_POINT;
        case sampler_filter::min_point_mag_linear_mip_point:
            return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
        case sampler_filter::min_linear_mag_mip_point:
            return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
        case sampler_filter::min_mag_linear_mip_point:
            return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        case sampler_filter::min_point_mag_mip_linear:
            return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
        case sampler_filter::min_linear_mag_point_mip_linear:
            return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        case sampler_filter::min_mag_point_mip_linear:
            return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
        case sampler_filter::min_mag_mip_linear:
            return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        case sampler_filter::anisotropic:
            return D3D12_FILTER_ANISOTROPIC;
        }
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

[[nodiscard]] constexpr D3D12_TEXTURE_ADDRESS_MODE to_native(sampler_address_mode mode)
{
    switch (mode)
    {
    case sampler_address_mode::wrap:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case sampler_address_mode::clamp:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case sampler_address_mode::clamp_border:
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case sampler_address_mode::mirror:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

[[nodiscard]] constexpr D3D12_COMPARISON_FUNC to_native(sampler_compare_func mode)
{
    switch (mode)
    {
    case sampler_compare_func::never:
    case sampler_compare_func::disabled:
        return D3D12_COMPARISON_FUNC_NEVER;
    case sampler_compare_func::less:
        return D3D12_COMPARISON_FUNC_LESS;
    case sampler_compare_func::equal:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case sampler_compare_func::less_equal:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case sampler_compare_func::greater:
        return D3D12_COMPARISON_FUNC_GREATER;
    case sampler_compare_func::not_equal:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case sampler_compare_func::greater_equal:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case sampler_compare_func::always:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_COMPARISON_FUNC_NEVER;
}

[[nodiscard]] constexpr D3D12_STATIC_BORDER_COLOR to_native(sampler_border_color color)
{
    switch (color)
    {
    case sampler_border_color::black_transparent_float:
    case sampler_border_color::black_transparent_int:
        return D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    case sampler_border_color::black_float:
    case sampler_border_color::black_int:
        return D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    case sampler_border_color::white_float:
    case sampler_border_color::white_int:
        return D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
}

[[nodiscard]] constexpr D3D12_QUERY_TYPE to_query_type(query_type type)
{
    switch (type)
    {
    case query_type::timestamp:
        return D3D12_QUERY_TYPE_TIMESTAMP;
    case query_type::occlusion:
        return D3D12_QUERY_TYPE_OCCLUSION;
    case query_type::pipeline_stats:
        return D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_QUERY_TYPE_TIMESTAMP;
}


[[nodiscard]] constexpr float to_opaque_border_color(sampler_border_color color)
{
    switch (color)
    {
    case sampler_border_color::black_transparent_float:
    case sampler_border_color::black_transparent_int:
    case sampler_border_color::black_float:
    case sampler_border_color::black_int:
        return 0.f;
    case sampler_border_color::white_float:
    case sampler_border_color::white_int:
        return 1.f;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return 1.f;
}

[[nodiscard]] constexpr float to_border_color_alpha(sampler_border_color color)
{
    switch (color)
    {
    case sampler_border_color::black_transparent_float:
    case sampler_border_color::black_transparent_int:
        return 0.f;
    case sampler_border_color::black_float:
    case sampler_border_color::black_int:
    case sampler_border_color::white_float:
    case sampler_border_color::white_int:
        return 1.f;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return 1.f;
}

[[nodiscard]] constexpr D3D12_RESOURCE_DIMENSION to_native(texture_dimension dim)
{
    switch (dim)
    {
    case texture_dimension::t1d:
        return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case texture_dimension::t2d:
        return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case texture_dimension::t3d:
        return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
}

[[nodiscard]] constexpr D3D12_LOGIC_OP to_native(blend_logic_op op)
{
    switch (op)
    {
    case blend_logic_op::no_op:
        return D3D12_LOGIC_OP_NOOP;
    case blend_logic_op::op_clear:
        return D3D12_LOGIC_OP_CLEAR;
    case blend_logic_op::op_set:
        return D3D12_LOGIC_OP_SET;
    case blend_logic_op::op_copy:
        return D3D12_LOGIC_OP_COPY;
    case blend_logic_op::op_copy_inverted:
        return D3D12_LOGIC_OP_COPY_INVERTED;
    case blend_logic_op::op_invert:
        return D3D12_LOGIC_OP_INVERT;
    case blend_logic_op::op_and:
        return D3D12_LOGIC_OP_AND;
    case blend_logic_op::op_nand:
        return D3D12_LOGIC_OP_NAND;
    case blend_logic_op::op_and_inverted:
        return D3D12_LOGIC_OP_AND_INVERTED;
    case blend_logic_op::op_and_reverse:
        return D3D12_LOGIC_OP_AND_REVERSE;
    case blend_logic_op::op_or:
        return D3D12_LOGIC_OP_OR;
    case blend_logic_op::op_nor:
        return D3D12_LOGIC_OP_NOR;
    case blend_logic_op::op_xor:
        return D3D12_LOGIC_OP_XOR;
    case blend_logic_op::op_or_reverse:
        return D3D12_LOGIC_OP_OR_REVERSE;
    case blend_logic_op::op_or_inverted:
        return D3D12_LOGIC_OP_OR_INVERTED;
    case blend_logic_op::op_equiv:
        return D3D12_LOGIC_OP_EQUIV;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_LOGIC_OP_NOOP;
}

[[nodiscard]] constexpr D3D12_BLEND_OP to_native(blend_op op)
{
    switch (op)
    {
    case blend_op::op_add:
        return D3D12_BLEND_OP_ADD;
    case blend_op::op_subtract:
        return D3D12_BLEND_OP_SUBTRACT;
    case blend_op::op_reverse_subtract:
        return D3D12_BLEND_OP_REV_SUBTRACT;
    case blend_op::op_min:
        return D3D12_BLEND_OP_MIN;
    case blend_op::op_max:
        return D3D12_BLEND_OP_MAX;
    }

    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_BLEND_OP_ADD;
}

[[nodiscard]] constexpr D3D12_BLEND to_native(blend_factor bf)
{
    switch (bf)
    {
    case blend_factor::zero:
        return D3D12_BLEND_ZERO;
    case blend_factor::one:
        return D3D12_BLEND_ONE;
    case blend_factor::src_color:
        return D3D12_BLEND_SRC_COLOR;
    case blend_factor::inv_src_color:
        return D3D12_BLEND_INV_SRC_COLOR;
    case blend_factor::src_alpha:
        return D3D12_BLEND_SRC_ALPHA;
    case blend_factor::inv_src_alpha:
        return D3D12_BLEND_INV_SRC_ALPHA;
    case blend_factor::dest_color:
        return D3D12_BLEND_DEST_COLOR;
    case blend_factor::inv_dest_color:
        return D3D12_BLEND_INV_DEST_COLOR;
    case blend_factor::dest_alpha:
        return D3D12_BLEND_DEST_ALPHA;
    case blend_factor::inv_dest_alpha:
        return D3D12_BLEND_INV_DEST_ALPHA;
    }


    CC_UNREACHABLE("to_native uncaught argument");
    return D3D12_BLEND_ZERO;
}

[[nodiscard]] constexpr D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS to_native_accel_struct_build_flags(accel_struct_build_flags_t flags)
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS res = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

    if (flags & accel_struct_build_flags::allow_update)
        res |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    if (flags & accel_struct_build_flags::allow_compaction)
        res |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
    if (flags & accel_struct_build_flags::prefer_fast_trace)
        res |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (flags & accel_struct_build_flags::prefer_fast_build)
        res |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    if (flags & accel_struct_build_flags::minimize_memory)
        res |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY;

    return res;
}

[[nodiscard]] constexpr D3D12_RESOURCE_FLAGS to_native_resource_usage_flags(resource_usage_flags_t flags)
{
    D3D12_RESOURCE_FLAGS res = D3D12_RESOURCE_FLAG_NONE;

    if (flags & resource_usage_flags::allow_uav)
        res |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (flags & resource_usage_flags::allow_depth_stencil)
        res |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (flags & resource_usage_flags::allow_render_target)
        res |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (flags & resource_usage_flags::deny_shader_resource)
        res |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    return res;
}
}
