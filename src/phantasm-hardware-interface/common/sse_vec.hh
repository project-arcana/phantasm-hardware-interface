#pragma once

#include <clean-core/macros.hh>

#include <emmintrin.h>

namespace phi
{
using SSEVec = __m128;
using SSEVecInt = __m128i;
using SSEVecDouble = __m128d;

//
// creation and access

CC_FORCE_INLINE SSEVec SSEVectorZero() { return _mm_setzero_ps(); }
CC_FORCE_INLINE float SSEGetComponent(SSEVec Vec, unsigned ComponentIndex) { return (((float*)&(Vec))[ComponentIndex]); }

// creates a vector consisting of the given float in all four components
CC_FORCE_INLINE SSEVec SSEReplicateToVector(void const* unaligned_val) { return _mm_load1_ps((float const*)unaligned_val); }

CC_FORCE_INLINE SSEVec SSEMakeVector(float x, float y, float z, float w) { return _mm_setr_ps(x, y, z, w); }

//
// load and store

CC_FORCE_INLINE SSEVec SSELoad(void const* unaligned_mem) { return _mm_loadu_ps((float*)unaligned_mem); }
CC_FORCE_INLINE void SSEStore(SSEVec const& vec, void* unaligned_mem) { _mm_storeu_ps((float*)unaligned_mem, vec); }

CC_FORCE_INLINE SSEVec SSELoadAligned(void const* mem) { return _mm_load_ps((float const*)(mem)); }
CC_FORCE_INLINE void SSEStoreAligned(SSEVec const& vec, void* mem) { _mm_store_ps((float*)mem, vec); }

// non-temporal store
CC_FORCE_INLINE void SSEStoreAlignedNoCache(SSEVec const& vec, void* mem) { _mm_stream_ps((float*)mem, vec); }

//
// compute

CC_FORCE_INLINE SSEVec SSEAdd(SSEVec const& lhs, SSEVec const& rhs) { return _mm_add_ps(lhs, rhs); }
CC_FORCE_INLINE SSEVec SSESubtract(SSEVec const& lhs, SSEVec const& rhs) { return _mm_sub_ps(lhs, rhs); }
CC_FORCE_INLINE SSEVec SSEMultiply(SSEVec const& lhs, SSEVec const& rhs) { return _mm_mul_ps(lhs, rhs); }


// returns the dot product of lhs and rhs in all four components
CC_FORCE_INLINE SSEVec SSEDot4(SSEVec const& lhs, SSEVec const& rhs)
{
#define PHI_SHUFFLE_MASK(A0, A1, B2, B3) ((A0) | ((A1) << 2) | ((B2) << 4) | ((B3) << 6))

    SSEVec t1, t2;
    t1 = SSEMultiply(lhs, rhs);
    t2 = _mm_shuffle_ps(t1, t1, PHI_SHUFFLE_MASK(2, 3, 0, 1)); // shuffle to zwxy
    t1 = SSEAdd(t1, t2);                                       // (xx + zz, yy + ww, zz + xx, ww + yy)
    t2 = _mm_shuffle_ps(t1, t1, PHI_SHUFFLE_MASK(1, 2, 3, 0)); // shuffle to yzwx
    return SSEAdd(t1, t2);                                     // (xx + zz + yy + ww, yy + ww + zz + xx, zz + xx + ww + yy, ww + yy + xx + zz)

#undef PHI_SHUFFLE_MASK
}
}
