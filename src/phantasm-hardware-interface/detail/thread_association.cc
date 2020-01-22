#include "thread_association.hh"

namespace
{
struct thread_index_info
{
    int ts_id = -1;
    unsigned index = 0;
};

int s_global_threadstorage_id = 0;
thread_local thread_index_info tl_index_info;

}

void pr::backend::detail::thread_association::initialize()
{
    _id = s_global_threadstorage_id++;
    _num_associations.store(0);
}

unsigned pr::backend::detail::thread_association::get_current_index()
{
    if (tl_index_info.ts_id != _id)
    {
        // this thread is unassociated (-1), or associated with a previous thread_association instance
        tl_index_info.ts_id = _id;
        tl_index_info.index = _num_associations.fetch_add(1);
    }

    return tl_index_info.index;
}
