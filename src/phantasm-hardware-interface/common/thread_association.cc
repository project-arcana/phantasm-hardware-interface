#include "thread_association.hh"

#include <clean-core/assert.hh>
#include <clean-core/intrinsics.hh>

namespace
{
struct ThreadIndexInfo
{
    int ts_id = -1;
    int index = 0;
};

int sGlobalThreadassocID = 0;
bool sGlobalThreadassocInUse = false;
thread_local ThreadIndexInfo tlsIndexInfo;
} // namespace

void phi::ThreadAssociation::initialize()
{
    // NOTE: this assert is overzealous, concurrent use of ThreadAssociation is possible, just not from
    // the same OS thread. As that would be a little harder to diagnose, this check will do for now.
    // the only way this assert is hit is if multiple phi::Backends are alive at the same time, if
    // that turns out to be a valid usecase, revisit
    CC_ASSERT_MSG(!sGlobalThreadassocInUse, "only one ThreadAssociation can be alive at a time\nif you really require multiple PHI "
                                            "backends concurrently, please contact the maintainers");

    sGlobalThreadassocInUse = true;

    mID = sGlobalThreadassocID++;
    mNumAssociations = 0;
}

void phi::ThreadAssociation::destroy()
{
    CC_ASSERT(sGlobalThreadassocInUse && "programmer error");
    sGlobalThreadassocInUse = false;
}

int phi::ThreadAssociation::getCurrentIndex()
{
    if (tlsIndexInfo.ts_id != mID)
    {
        // this thread is unassociated (-1), or associated with a previous ThreadAssociation instance
        tlsIndexInfo.ts_id = mID;
        tlsIndexInfo.index = cc::intrin_atomic_add(&mNumAssociations, 1);
    }

    return tlsIndexInfo.index;
}
