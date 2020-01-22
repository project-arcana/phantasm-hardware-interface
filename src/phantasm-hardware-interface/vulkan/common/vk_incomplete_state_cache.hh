#pragma once

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

#include <phantasm-hardware-interface/types.hh>

namespace phi::vk
{
struct vk_incomplete_state_cache
{
public:
    /// signal a resource transition to a given state
    /// returns true if the before state is known, or false otherwise
    bool transition_resource(handle::resource res, resource_state after, VkPipelineStageFlags after_dependencies, resource_state& out_before, VkPipelineStageFlags& out_before_dependency)
    {
        for (auto& entry : cache)
        {
            if (entry.ptr == res)
            {
                // resource is in cache
                out_before = entry.current;
                out_before_dependency = entry.current_dependency;
                entry.current = after;
                entry.current_dependency = after_dependencies;
                return true;
            }
        }

        cache.push_back({res, after, after, after_dependencies, after_dependencies});
        return false;
    }

    void reset() { cache.clear(); }

public:
    struct cache_entry
    {
        /// (const) the resource handle
        handle::resource ptr;
        /// (const) the <after> state of the initial barrier (<before> is unknown)
        resource_state required_initial;
        /// latest state of this resource
        resource_state current;
        /// the first pipeline stage touching this resource
        VkPipelineStageFlags initial_dependency;
        /// the latest pipeline stage to touch this resource
        VkPipelineStageFlags current_dependency;
    };

    // linear "map" for now, might want to benchmark this
    cc::capped_vector<cache_entry, 32> cache;
};
}
