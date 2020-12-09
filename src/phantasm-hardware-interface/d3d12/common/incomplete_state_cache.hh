#pragma once

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/types.hh>

#include "d3d12_fwd.hh"

namespace phi::d3d12
{
/// A thread-local, incomplete-information resource state cache
/// Keeps track of locally known resource states, and stores the required initial states
/// After use:
///     1. command list and incomplete state cache are passed to submission thread
///     2. submission thread creates an additional, small command list to be executed first
///     3. goes through the master state cache to find all the unknown <before> states
///     4. creates barriers for all cache entries, transitioning from (known) <before> to cache_entry::required_initial
///     5. executes small "barrier" command list, then executes the proper command list, now with all states correctly in place
///     6. updates master cache with all the cache_entry::current states
struct incomplete_state_cache
{
    struct cache_entry
    {
        /// (const) the resource handle
        handle::resource ptr;
        /// (const) the <after> state of the initial barrier (<before> is unknown)
        D3D12_RESOURCE_STATES required_initial;
        /// latest state of this resource
        D3D12_RESOURCE_STATES current;
    };

    /// signal a resource transition to a given state
    /// returns true if the before state is known, or false otherwise
    bool transition_resource(handle::resource res, D3D12_RESOURCE_STATES after, D3D12_RESOURCE_STATES& out_before)
    {
        for (auto i = 0u; i < num_entries; ++i)
        {
            cache_entry& entry = entries[i];
            if (entry.ptr == res)
            {
                // resource is in cache
                out_before = entry.current;
                entry.current = after;
                return true;
            }
        }

        CC_ASSERT(num_entries < entries.size() && "state cache full, increase increase PHI config : max_num_unique_transitions_per_cmdlist");
        entries[num_entries++] = {res, after, after};
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
