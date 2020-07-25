#pragma once

#include <atomic>

namespace phi::detail
{
/// an association between threads and incrementing indices
/// if get_current is called from n unique threads, they will each receive
/// a unique index in [0, n-1] (and continue to receive the same one)
///
/// each OS thread can only be tied to a single thread_association at a time
struct thread_association
{
    void initialize();
    void destroy();
    [[nodiscard]] unsigned get_current_index();

private:
    int _id;
    std::atomic_uint _num_associations;
};

}
