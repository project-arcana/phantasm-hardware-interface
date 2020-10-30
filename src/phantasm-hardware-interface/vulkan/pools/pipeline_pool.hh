#pragma once

#include <mutex>

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/common/container/linked_pool.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>
#include <phantasm-hardware-interface/vulkan/resources/descriptor_allocator.hh>

#include "pipeline_layout_cache.hh"
#include "render_pass_cache.hh"

namespace phi::vk
{
/// The high-level allocator for PSOs and root signatures
/// Synchronized
class PipelinePool
{
public:
    // frontend-facing API

    [[nodiscard]] handle::pipeline_state createPipelineState(arg::vertex_format vertex_format,
                                                             const arg::framebuffer_config& framebuffer_config,
                                                             arg::shader_arg_shapes shader_arg_shapes,
                                                             bool should_have_push_constants,
                                                             arg::graphics_shaders shader_stages,
                                                             phi::pipeline_config const& primitive_config,
                                                             cc::allocator* scratch_alloc);

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::shader_arg_shapes shader_arg_shapes,
                                                                    arg::shader_binary compute_shader,
                                                                    bool should_have_push_constants,
                                                                    cc::allocator* scratch_alloc);

    [[nodiscard]] handle::pipeline_state createRaytracingPipelineState(cc::span<arg::raytracing_shader_library const> libraries,
                                                                       cc::span<arg::raytracing_argument_association const> arg_assocs,
                                                                       cc::span<arg::raytracing_hit_group const> hit_groups,
                                                                       unsigned max_recursion,
                                                                       unsigned max_payload_size_bytes,
                                                                       unsigned max_attribute_size_bytes,
                                                                       cc::allocator* scratch_alloc);

    void free(handle::pipeline_state ps);

public:
    struct pso_node
    {
        VkPipeline raw_pipeline;
        pipeline_layout* associated_pipeline_layout;
    };

public:
    // internal API

    void initialize(VkDevice device, unsigned max_num_psos, cc::allocator* static_alloc);
    void destroy();

    [[nodiscard]] pso_node const& get(handle::pipeline_state ps) const { return mPool.get(ps._value); }

    [[nodiscard]] VkRenderPass getOrCreateRenderPass(cmd::begin_render_pass const& brp_cmd, int num_samples, cc::span<format const> rt_formats);

private:
    VkDevice mDevice;
    PipelineLayoutCache mLayoutCache;
    RenderPassCache mRenderPassCache;
    DescriptorAllocator mDescriptorAllocator;
    phi::linked_pool<pso_node> mPool;
    std::mutex mMutex;
};

}
