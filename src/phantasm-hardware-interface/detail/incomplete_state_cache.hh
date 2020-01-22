#pragma once

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/types.hh>

namespace pr::backend::detail
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
template<class StateT>
struct generic_incomplete_state_cache
{
public:
    /// signal a resource transition to a given state
    /// returns true if the before state is known, or false otherwise
    bool transition_resource(handle::resource res, StateT after, StateT& out_before)
    {
        for (auto& entry : cache)
        {
            if (entry.ptr == res)
            {
                // resource is in cache
                out_before = entry.current;
                entry.current = after;
                return true;
            }
        }

        cache.push_back({res, after, after});
        return false;
    }

    void reset() { cache.clear(); }

public:
    struct cache_entry
    {
        /// (const) the resource handle
        handle::resource ptr;
        /// (const) the <after> state of the initial barrier (<before> is unknown)
        StateT required_initial;
        /// latest state of this resource
        StateT current;
    };

    // linear "map" for now, might want to benchmark this
    cc::capped_vector<cache_entry, 32> cache;
};

using incomplete_state_cache = generic_incomplete_state_cache<resource_state>;

}
