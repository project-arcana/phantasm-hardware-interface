#include "render_pass_cache.hh"

#include <phantasm-hardware-interface/detail/hash.hh>
#include <phantasm-hardware-interface/vulkan/render_pass_pipeline.hh>

void pr::backend::vk::RenderPassCache::initialize(unsigned max_elements) { mCache.initialize(max_elements); }

void pr::backend::vk::RenderPassCache::destroy(VkDevice device) { reset(device); }

VkRenderPass pr::backend::vk::RenderPassCache::getOrCreate(VkDevice device, cmd::begin_render_pass const& brp, unsigned num_samples, cc::span<const format> override_rt_formats)
{
    // render passes, and the hash, only depend on RT formats, clear ops, and the amount of samples
    auto const hash = hashKey(brp, num_samples, override_rt_formats);

    auto* const lookup = mCache.look_up(hash);
    if (lookup != nullptr)
        return *lookup;
    else
    {
        auto const new_rp = create_render_pass(device, brp, num_samples, override_rt_formats);
        auto* const insertion = mCache.insert(hash, new_rp);
        return *insertion;
    }
}

void pr::backend::vk::RenderPassCache::reset(VkDevice device)
{
    mCache.iterate_elements([&](VkRenderPass elem) { vkDestroyRenderPass(device, elem, nullptr); });
    mCache.clear();
}

cc::hash_t pr::backend::vk::RenderPassCache::hashKey(cmd::begin_render_pass const& brp, unsigned num_samples, cc::span<const format> override_rt_formats)
{
    cc::hash_t res = 0;
    for (uint8_t i = 0u; i < brp.render_targets.size(); ++i)
    {
        res = cc::hash_combine(res, cc::make_hash(brp.render_targets[i].clear_type, override_rt_formats[i]));
    }
    if (brp.depth_target.sve.resource != handle::null_resource)
    {
        auto const& ds = brp.depth_target;
        res = cc::hash_combine(res, cc::make_hash(ds.clear_type, ds.sve.pixel_format));
    }

    return cc::hash_combine(res, cc::make_hash(num_samples));
}
