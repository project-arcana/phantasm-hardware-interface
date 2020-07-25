#include "thread_association.hh"

#include <clean-core/assert.hh>

namespace
{
struct thread_index_info
{
    int ts_id = -1;
    unsigned index = 0;
};

int s_global_threadassoc_id = 0;
bool s_global_threadalloc_in_use = false;
thread_local thread_index_info tl_index_info;

}

void phi::detail::thread_association::initialize()
{
    // NOTE: this assert is overzealous, concurrent use of thread_association is possible, just not from
    // the same OS thread. As that would be a little harder to diagnose, this check will do for now.
    // the only way this assert is hit is if multiple phi::Backends are alive at the same time, if
    // that turns out to be a valid usecase, revisit
    CC_ASSERT_MSG(!s_global_threadalloc_in_use, "only one thread_association can be alive at a time\nif you really require multiple PHI "
                                                "backends concurrently, please contact the maintainers");

    s_global_threadalloc_in_use = true;

    _id = s_global_threadassoc_id++;
    _num_associations.store(0);
}

void phi::detail::thread_association::destroy()
{
    CC_ASSERT(s_global_threadalloc_in_use && "programmer error");
    s_global_threadalloc_in_use = false;
}

unsigned phi::detail::thread_association::get_current_index()
{
    if (tl_index_info.ts_id != _id)
    {
        // this thread is unassociated (-1), or associated with a previous thread_association instance
        tl_index_info.ts_id = _id;
        tl_index_info.index = _num_associations.fetch_add(1);
    }

    return tl_index_info.index;
}
