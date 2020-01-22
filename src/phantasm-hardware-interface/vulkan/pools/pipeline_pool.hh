#pragma once

#include <mutex>

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/detail/linked_pool.hh>
#include <phantasm-hardware-interface/types.hh>
#include <phantasm-hardware-interface/primitive_pipeline_config.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>
#include <phantasm-hardware-interface/vulkan/resources/descriptor_allocator.hh>

#include "pipeline_layout_cache.hh"
#include "render_pass_cache.hh"

namespace pr::backend::vk
{
/// The high-level allocator for PSOs and root signatures
/// Synchronized
class PipelinePool
{
public:
    // frontend-facing API

    [[nodiscard]] handle::pipeline_state createPipelineState(arg::vertex_format vertex_format,
                                                             const arg::framebuffer_config& framebuffer_config,
                                                             arg::shader_argument_shapes shader_arg_shapes,
                                                             bool should_have_push_constants,
                                                             arg::graphics_shader_stages shader_stages,
                                                             pr::primitive_pipeline_config const& primitive_config);

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::shader_argument_shapes shader_arg_shapes,
                                                                    arg::shader_binary compute_shader,
                                                                    bool should_have_push_constants);

    void free(handle::pipeline_state ps);

public:
    struct pso_node
    {
        VkPipeline raw_pipeline;
        pipeline_layout* associated_pipeline_layout;

        // info stored which is required for creating render passes on the fly / cached
        cc::capped_vector<format, limits::max_render_targets> rt_formats;
        unsigned num_msaa_samples;
    };

public:
    // internal API

    void initialize(VkDevice device, unsigned max_num_psos);
    void destroy();

    [[nodiscard]] pso_node const& get(handle::pipeline_state ps) const { return mPool.get(static_cast<unsigned>(ps.index)); }

    [[nodiscard]] VkRenderPass getOrCreateRenderPass(pso_node const& node, cmd::begin_render_pass const& brp_cmd);

private:
    VkDevice mDevice;
    PipelineLayoutCache mLayoutCache;
    RenderPassCache mRenderPassCache;
    DescriptorAllocator mDescriptorAllocator;
    backend::detail::linked_pool<pso_node, unsigned> mPool;
    std::mutex mMutex;
};

}
