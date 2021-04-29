#pragma once

#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/common/container/stable_map.hh>

#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-hardware-interface/vulkan/loader/vulkan_fwd.hh>
#include <phantasm-hardware-interface/vulkan/pipeline_layout.hh>

namespace phi::vk
{
class DescriptorAllocator;

/// Persistent cache for pipeline layouts
/// Unsynchronized, only used inside of pipeline pool
class PipelineLayoutCache
{
public:
    void initialize(unsigned max_elements, cc::allocator* static_alloc);
    void destroy(VkDevice device);

    /// receive an existing root signature matching the shape, or create a new one
    /// returns a pointer which remains stable
    [[nodiscard]] pipeline_layout* getOrCreate(VkDevice device, cc::span<util::spirv_desc_info const> reflected_ranges, bool has_push_constants);

    /// destroys all elements inside, and clears the map
    void reset(VkDevice device);

private:
    static size_t hashKey(cc::span<util::spirv_desc_info const> reflected_ranges, bool has_push_constants);

    struct pipeline_layout_key_readonly
    {
        cc::span<util::spirv_desc_info const> ranges;
        bool has_push_constants;
    };

    struct pipeline_layout_key
    {
        cc::vector<util::spirv_desc_info> ranges;
        bool has_push_constants;

        pipeline_layout_key() = default;
        pipeline_layout_key(pipeline_layout_key_readonly const& ro) : ranges(ro.ranges), has_push_constants(ro.has_push_constants) {}

        bool operator==(pipeline_layout_key_readonly const& lhs) const noexcept
        {
            return has_push_constants == lhs.has_push_constants && ranges == lhs.ranges;
        }
    };

    struct pipeline_layout_hasher
    {
        uint64_t operator()(pipeline_layout_key_readonly const& v) const noexcept
        {
            size_t res = cc::make_hash(v.has_push_constants);
            for (auto const& elem : v.ranges)
            {
                auto const elem_hash = cc::make_hash(elem.set, elem.type, elem.binding, elem.binding_array_size, elem.visible_stage);
                res = cc::hash_combine(res, elem_hash);
            }
            return res;
        }

        uint64_t operator()(pipeline_layout_key const& v) const noexcept
        {
            size_t res = cc::make_hash(v.has_push_constants);
            for (auto const& elem : v.ranges)
            {
                auto const elem_hash = cc::make_hash(elem.set, elem.type, elem.binding, elem.binding_array_size, elem.visible_stage);
                res = cc::hash_combine(res, elem_hash);
            }
            return res;
        }
    };

    phi::detail::stable_map<pipeline_layout_key, pipeline_layout, pipeline_layout_hasher> mCache;
};

}
