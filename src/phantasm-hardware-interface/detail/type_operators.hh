#pragma once

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/types.hh>

namespace phi
{
// this header contains operators required for using various phi types as hashes
// internally unused

[[maybe_unused]] inline constexpr bool operator==(sampler_config const& lhs, sampler_config const& rhs) noexcept
{
    return lhs.filter == rhs.filter &&                 //
           lhs.address_u == rhs.address_u &&           //
           lhs.address_v == rhs.address_v &&           //
           lhs.address_w == rhs.address_w &&           //
           lhs.min_lod == rhs.min_lod &&               //
           lhs.max_lod == rhs.max_lod &&               //
           lhs.lod_bias == rhs.lod_bias &&             //
           lhs.max_anisotropy == rhs.max_anisotropy && //
           lhs.compare_func == rhs.compare_func &&     //
           lhs.border_color == rhs.border_color;       //
}


[[maybe_unused]] inline constexpr bool operator==(pipeline_config const& lhs, pipeline_config const& rhs) noexcept
{
    return lhs.topology == rhs.topology && lhs.depth == rhs.depth && lhs.depth_readonly == rhs.depth_readonly && lhs.cull == rhs.cull && lhs.samples == rhs.samples;
}

[[maybe_unused]] inline constexpr bool operator==(render_target_config const& lhs, render_target_config const& rhs) noexcept
{
    return lhs.format == rhs.format &&                     //
           lhs.blend_enable == rhs.blend_enable &&         //
           lhs.blend_color_src == rhs.blend_color_src &&   //
           lhs.blend_color_dest == rhs.blend_color_dest && //
           lhs.blend_op_color == rhs.blend_op_color &&     //
           lhs.blend_alpha_src == rhs.blend_alpha_src &&   //
           lhs.blend_alpha_dest == rhs.blend_alpha_dest && //
           lhs.blend_op_alpha == rhs.blend_op_alpha;
}

[[maybe_unused]] inline constexpr bool operator==(vertex_attribute_info const& lhs, vertex_attribute_info const& rhs) noexcept
{
    // note: we ignore semantic names here intentionally
    return lhs.offset == rhs.offset && lhs.format == rhs.format;
}

[[maybe_unused]] inline constexpr bool operator==(arg::framebuffer_config const& lhs, arg::framebuffer_config const& rhs) noexcept
{
    return lhs.render_targets == rhs.render_targets && lhs.logic_op_enable == rhs.logic_op_enable && lhs.logic_op == rhs.logic_op
           && lhs.depth_target == rhs.depth_target;
}
}
