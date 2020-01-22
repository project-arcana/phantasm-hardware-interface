#pragma once

namespace pr::backend::limits
{
/// the maximum amount of render targets per render pass, excluding the depthstencil target
/// NOTE: D3D12 only supports up to 8 render targets
inline constexpr unsigned max_render_targets = 8u;

/// the maximum amount of resource transitions per transition command
/// configurable
inline constexpr unsigned max_resource_transitions = 4u;

/// the maximum amount of shader arguments per draw- or compute dispatch command
/// NOTE: The Vulkan backend requires (2 * max_shader_arguments) descriptor sets,
/// most non-desktop GPUs only support a maximum of 8.
inline constexpr unsigned max_shader_arguments = 4u;

/// the maximum amount of samplers per shader view
/// configurable
inline constexpr unsigned max_shader_samplers = 16u;

/// the maximum size for compute root constants
/// configurable in increments of sizeof(DWORD32)
inline constexpr unsigned max_root_constant_bytes = 8u;

/// the maximum amount of argument associations
/// configurable
inline constexpr unsigned max_raytracing_argument_assocs = 8u;

/// the maximum amount of hit groups
/// configurable
inline constexpr unsigned max_raytracing_hit_groups = 16u;
}
