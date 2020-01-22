#pragma once

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/detail/cache_map.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/vulkan_fwd.hh>

namespace pr::backend::cmd
{
struct begin_render_pass;
}

namespace pr::backend::vk
{
/// Persistent cache for render passes
/// Unsynchronized, only used inside of pipeline pool
class RenderPassCache
{
public:
    void initialize(unsigned max_elements);
    void destroy(VkDevice device);

    /// receive an existing render pass matching the framebuffer formats and config, or create a new one
    /// While pixel format information IS present in cmd::begin_render_pass, it is invalid if that RT is a backbuffer, which is why
    /// the additional override_rt_formats span is passed
    [[nodiscard]] VkRenderPass getOrCreate(VkDevice device, cmd::begin_render_pass const& brp, unsigned num_samples, cc::span<format const> override_rt_formats);

    /// destroys all elements inside, and clears the map
    void reset(VkDevice device);

private:
    static cc::hash_t hashKey(cmd::begin_render_pass const& brp, unsigned num_samples, cc::span<const format> override_rt_formats);

    backend::detail::cache_map<VkRenderPass> mCache;
};

}
