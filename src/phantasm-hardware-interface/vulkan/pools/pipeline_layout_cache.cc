#include "pipeline_layout_cache.hh"

#include <phantasm-hardware-interface/detail/hash.hh>

void pr::backend::vk::PipelineLayoutCache::initialize(unsigned max_elements) { mCache.initialize(max_elements); }

void pr::backend::vk::PipelineLayoutCache::destroy(VkDevice device) { reset(device); }

pr::backend::vk::pipeline_layout* pr::backend::vk::PipelineLayoutCache::getOrCreate(VkDevice device, cc::span<const util::spirv_desc_info> reflected_ranges, bool has_push_constants)
{
    auto const hash = hashKey(reflected_ranges, has_push_constants);

    auto* const lookup = mCache.look_up(hash);
    if (lookup != nullptr)
        return lookup;
    else
    {
        auto* const insertion = mCache.insert(hash, pipeline_layout{});
        insertion->initialize(device, reflected_ranges, has_push_constants);
        return insertion;
    }
}

void pr::backend::vk::PipelineLayoutCache::reset(VkDevice device)
{
    mCache.iterate_elements([&](pipeline_layout& elem) { elem.free(device); });
    mCache.clear();
}

size_t pr::backend::vk::PipelineLayoutCache::hashKey(cc::span<const pr::backend::vk::util::spirv_desc_info> reflected_ranges, bool has_push_constants)
{
    size_t res = cc::make_hash(has_push_constants);
    for (auto const& elem : reflected_ranges)
    {
        auto const elem_hash = cc::make_hash(elem.set, elem.type, elem.binding, elem.binding_array_size, elem.visible_stage);
        res = cc::hash_combine(res, elem_hash);
    }
    return res;
}
