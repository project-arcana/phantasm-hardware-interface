#pragma once

#include <clean-core/alloc_vector.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/span.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/commands.hh>

#include "loader/volk.hh"

namespace phi::vk
{
namespace util
{
struct patched_spirv_stage;
}
struct patched_shader_intermediates;

[[nodiscard]] VkRenderPass create_render_pass(VkDevice device, const arg::framebuffer_config& framebuffer, phi::pipeline_config const& config);

[[nodiscard]] VkRenderPass create_render_pass(VkDevice device, const phi::cmd::begin_render_pass& begin_rp, unsigned num_samples, cc::span<const format> override_rt_formats);

[[nodiscard]] VkPipeline create_pipeline(VkDevice device,
                                         VkRenderPass render_pass,
                                         VkPipelineLayout pipeline_layout,
                                         cc::span<util::patched_spirv_stage const> shaders,
                                         phi::pipeline_config const& config,
                                         cc::span<VkVertexInputAttributeDescription const> vertex_attribs,
                                         uint32_t vertex_size,
                                         const arg::framebuffer_config& framebuf_config);

[[nodiscard]] VkPipeline create_compute_pipeline(VkDevice device, VkPipelineLayout pipeline_layout, const util::patched_spirv_stage& compute_shader);

VkPipeline create_raytracing_pipeline(VkDevice device,
                                      const phi::vk::patched_shader_intermediates& shader_intermediates,
                                      VkPipelineLayout pipeline_layout,
                                      cc::span<phi::arg::raytracing_argument_association const> arg_assocs,
                                      cc::span<phi::arg::raytracing_hit_group const> hit_groups,
                                      unsigned max_recursion,
                                      unsigned max_payload_size_bytes,
                                      unsigned max_attribute_size_bytes,
                                      cc::allocator* scratch_alloc);
}
