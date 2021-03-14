#pragma once

namespace phi::limits
{
enum limits_e : unsigned
{
    /// the maximum amount of render targets per render pass, excluding the depthstencil target
    /// NOTE: D3D12 only supports up to 8 render targets
    max_render_targets = 8u,

    /// the maximum amount of resource transitions per transition command
    /// configurable
    max_resource_transitions = 4u,

    /// the maximum amount of UAV barriers per command
    /// configurable
    max_uav_barriers = 8u,

    /// the maximum amount of shader arguments per draw- or compute dispatch command
    /// NOTE: The Vulkan backend requires (2 * max_shader_arguments) descriptor sets,
    /// most non-desktop GPUs only support a maximum of 8.
    max_shader_arguments = 4u,

    /// the maximum amount of samplers per shader view
    /// configurable
    max_shader_samplers = 16u,

    /// the maximum size for root constants
    /// configurable in increments of 4, also concerns CPU memory (cmd::draw, cmd::dispatch)
    max_root_constant_bytes = 16u,

    /// the maximum amount of usable vertex buffers (per drawcall and in a graphics PSO)
    max_vertex_buffers = 4u,

    /// the maximum amount of argument associations
    /// configurable
    max_raytracing_argument_assocs = 8u,

    /// the maximum amount of hit groups
    /// configurable
    max_raytracing_hit_groups = 16u,

    /// amount of shader stages in the graphics pipeline
    num_graphics_shader_stages = 5u,
};
}
