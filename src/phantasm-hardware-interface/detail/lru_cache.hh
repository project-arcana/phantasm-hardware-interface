#pragma once

namespace phi
{
namespace handle
{
struct event
{
    int val;
};
}

template <class KeyT, class ValT>
struct lru_cache
{
    ValT* acquire(KeyT const& key)
    {
        // if (has_free_val)
        //      return free
        // elif (has_pending_vals)
        //      check pending
        //      return if any is free
        //
        // create & return new

        // result state is acquired
    }

    void free(ValT* val, handle::event dependency)
    {
        // assert(val.state == acquired)
        // change state to pending
        // add dependency

        // free things (?)
    }

private:
    enum class element_state
    {
        free,
        acquired,
        pending
    };

    struct element
    {
        ValT val;
        handle::event current_dependency;
        element_state state;
    };
};

}
