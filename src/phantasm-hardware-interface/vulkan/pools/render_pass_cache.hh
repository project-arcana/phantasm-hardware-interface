#pragma once

#include <clean-core/capped_vector.hh>
#include <clean-core/span.hh>

#include <phantasm-hardware-interface/commands.hh>
#include <phantasm-hardware-interface/detail/stable_map.hh>
#include <phantasm-hardware-interface/limits.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/vulkan_fwd.hh>

namespace phi::cmd
{
struct begin_render_pass;
}

namespace phi::vk
{
/// Persistent cache for render passes
/// Unsynchronized, only used inside of pipeline pool
class RenderPassCache
{
public:
    void initialize(unsigned max_elements, cc::allocator* static_alloc);
    void destroy(VkDevice device);

    /// receive an existing render pass matching the framebuffer formats and config, or create a new one
    /// While pixel format information IS present in cmd::begin_render_pass, it is invalid if that RT is a backbuffer, which is why
    /// the additional override_rt_formats span is passed
    [[nodiscard]] VkRenderPass getOrCreate(VkDevice device, cmd::begin_render_pass const& brp, unsigned num_samples, cc::span<format const> override_rt_formats);

    /// destroys all elements inside, and clears the map
    void reset(VkDevice device);

private:
    static cc::hash_t hashKey(cmd::begin_render_pass const& brp, unsigned num_samples, cc::span<const format> override_rt_formats);

    struct render_pass_key_readonly
    {
        cmd::begin_render_pass const& brp;
        unsigned num_samples;
        cc::span<format const> override_formats;
    };

    struct render_pass_key
    {
        cmd::begin_render_pass brp;
        unsigned num_samples;
        cc::capped_vector<format, limits::max_render_targets> override_formats;

        render_pass_key() = default;
        render_pass_key(render_pass_key_readonly const& ro) : brp(ro.brp), num_samples(ro.num_samples), override_formats(ro.override_formats) {}
        bool operator==(render_pass_key_readonly const& rhs) const noexcept;
    };

    struct render_pass_hasher
    {
        cc::hash_t operator()(render_pass_key_readonly const& v) const noexcept { return hashKey(v.brp, v.num_samples, v.override_formats); }
        cc::hash_t operator()(render_pass_key const& v) const noexcept { return hashKey(v.brp, v.num_samples, v.override_formats); }
    };

    phi::detail::stable_map<render_pass_key, VkRenderPass, render_pass_hasher> mCache;
};

}
