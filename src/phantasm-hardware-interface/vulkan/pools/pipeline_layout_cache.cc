#include "pipeline_layout_cache.hh"

#include <phantasm-hardware-interface/common/hash.hh>

void phi::vk::PipelineLayoutCache::initialize(unsigned max_elements, cc::allocator* static_alloc) { mCache.initialize(max_elements, static_alloc); }

void phi::vk::PipelineLayoutCache::destroy(VkDevice device) { reset(device); }

phi::vk::pipeline_layout* phi::vk::PipelineLayoutCache::getOrCreate(VkDevice device, cc::span<const util::spirv_desc_info> reflected_ranges, bool has_push_constants)
{
    auto const readonly_key = pipeline_layout_key_readonly{reflected_ranges, has_push_constants};

    pipeline_layout& val = mCache[readonly_key];
    if (val.raw_layout == nullptr)
    {
        val.initialize(device, reflected_ranges, has_push_constants);
    }

    return &val;
}

void phi::vk::PipelineLayoutCache::reset(VkDevice device)
{
    mCache.iterate_elements([&](pipeline_layout& elem) { elem.free(device); });
    mCache.reset();
}

size_t phi::vk::PipelineLayoutCache::hashKey(cc::span<const phi::vk::util::spirv_desc_info> reflected_ranges, bool has_push_constants)
{
    size_t res = cc::make_hash(has_push_constants);
    for (auto const& elem : reflected_ranges)
    {
        auto const elem_hash = cc::make_hash(elem.set, elem.type, elem.binding, elem.binding_array_size, elem.visible_stage);
        res = cc::hash_combine(res, elem_hash);
    }
    return res;
}
