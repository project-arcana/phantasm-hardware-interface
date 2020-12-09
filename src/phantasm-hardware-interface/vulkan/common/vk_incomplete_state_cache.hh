#pragma once

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

#include <phantasm-hardware-interface/types.hh>

namespace phi::vk
{
struct vk_incomplete_state_cache
{
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

    /// signal a resource transition to a given state
    /// returns true if the before state is known, or false otherwise
    bool transition_resource(handle::resource res, resource_state after, VkPipelineStageFlags after_dependencies, resource_state& out_before, VkPipelineStageFlags& out_before_dependency)
    {
        for (auto i = 0u; i < num_entries; ++i)
        {
            cache_entry& entry = entries[i];
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

        CC_ASSERT(num_entries < entries.size() && "state cache full, increase PHI config : max_num_unique_transitions_per_cmdlist");
        entries[num_entries++] = {res, after, after, after_dependencies, after_dependencies};
        return false;
    }

    void reset() { num_entries = 0; }

    void initialize(cc::span<cache_entry> memory)
    {
        num_entries = 0;
        entries = memory;
    }

    // linear map for now
    unsigned num_entries = 0;
    cc::span<cache_entry> entries;
};
}
