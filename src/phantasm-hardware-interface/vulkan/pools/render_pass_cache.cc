#include "render_pass_cache.hh"

#include <phantasm-hardware-interface/common/hash.hh>
#include <phantasm-hardware-interface/vulkan/render_pass_pipeline.hh>

void phi::vk::RenderPassCache::initialize(unsigned max_elements, cc::allocator* static_alloc)
{
    mCache.initialize(max_elements, static_alloc);
    mCache.memset_values_zero(); // we're dealing with plain pointers, memset to nullptr
}

void phi::vk::RenderPassCache::destroy(VkDevice device) { reset(device); }

VkRenderPass phi::vk::RenderPassCache::getOrCreate(VkDevice device, cmd::begin_render_pass const& brp, unsigned num_samples, cc::span<const format> override_rt_formats)
{
    auto const readonly_key = render_pass_key_readonly{brp, num_samples, override_rt_formats};

    VkRenderPass& val = mCache[readonly_key];
    if (val == nullptr)
    {
        val = create_render_pass(device, brp, num_samples, override_rt_formats);
    }

    return val;
}

void phi::vk::RenderPassCache::reset(VkDevice device)
{
    mCache.iterate_elements([&](VkRenderPass elem) { vkDestroyRenderPass(device, elem, nullptr); });
    mCache.reset();
    mCache.memset_values_zero();
}

cc::hash_t phi::vk::RenderPassCache::hashKey(cmd::begin_render_pass const& brp, unsigned num_samples, cc::span<const format> override_rt_formats)
{
    cc::hash_t res = 0;
    for (uint8_t i = 0u; i < brp.render_targets.size(); ++i)
    {
        res = cc::hash_combine(res, cc::make_hash(brp.render_targets[i].clear_type, override_rt_formats[i]));
    }
    if (brp.depth_target.rv.resource != handle::null_resource)
    {
        auto const& ds = brp.depth_target;
        res = cc::hash_combine(res, cc::make_hash(ds.clear_type, ds.rv.pixel_format));
    }

    return cc::hash_combine(res, cc::make_hash(num_samples));
}

bool phi::vk::RenderPassCache::render_pass_key::operator==(const phi::vk::RenderPassCache::render_pass_key_readonly& rhs) const noexcept
{
    // comparison includes only the relevant parts of cmd::begin_render_pass

    if (num_samples == rhs.num_samples && override_formats == rhs.override_formats)
    {
        if (brp.render_targets.size() != rhs.brp.render_targets.size())
            return false;

        for (uint8_t i = 0u; i < brp.render_targets.size(); ++i)
        {
            if (brp.render_targets[i].clear_type != rhs.brp.render_targets[i].clear_type)
                return false;
        }

        if (brp.depth_target.rv.resource.is_valid())
        {
            if (!rhs.brp.depth_target.rv.resource.is_valid())
                return false;

            auto const& lhs_dt = brp.depth_target;
            auto const& rhs_dt = rhs.brp.depth_target;

            if (lhs_dt.rv.pixel_format != rhs_dt.rv.pixel_format || lhs_dt.clear_type != rhs_dt.clear_type)
                return false;
        }

        return true;
    }
    else
    {
        return true;
    }
}
