#pragma once

namespace phi
{
/// an association between threads and incrementing indices
/// if get_current is called from n unique threads, they will each receive
/// a unique index in [0, n-1] (and continue to receive the same one)
///
/// each OS thread can only be tied to a single ThreadAssociation at a time
struct ThreadAssociation
{
    void initialize();
    void destroy();

    int getCurrentIndex();

private:
    int mID;
    int volatile mNumAssociations;
};

} // namespace phi
