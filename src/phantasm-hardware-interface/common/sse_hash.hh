#pragma once

#include <cstdint>

#include <clean-core/macros.hh>

// disable SSE usage by defining PHI_USE_SSE_HASH to 0
// requires SSE4.2 - Intel Nehalem (Nov 2008) and AMD Bulldozer (Oct 2011)
#ifndef PHI_USE_SSE_HASH
#define PHI_USE_SSE_HASH 1
#endif

#if PHI_USE_SSE_HASH
#include <clean-core/utility.hh>

#ifdef CC_COMPILER_MSVC
#include <intrin.h>
#pragma intrinsic(_mm_crc32_u32)
#pragma intrinsic(_mm_crc32_u64)
#else
#include <smmintrin.h>
#endif
#endif

namespace phi::util
{
inline uint64_t sse_hash(const uint32_t* begin, const uint32_t* end, uint64_t initial_hash = 2166136261U)
{
    // Modified from D3D12 MiniEngine, Hash.h
    // Original license:
    //
    // Copyright (c) Microsoft. All rights reserved.
    // This code is licensed under the MIT License (MIT).
    // THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
    // ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
    // IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
    // PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
    //
    // Developed by Minigraph
    //
    // Author:  James Stanard

#if PHI_USE_SSE_HASH
    const uint64_t* iter64 = (const uint64_t*)cc::align_up(begin, 8);
    const uint64_t* const end64 = (const uint64_t* const)cc::align_down(end, 8);

    // If not 64-bit aligned, start with a single u32
    if ((uint32_t*)iter64 > begin)
        initial_hash = _mm_crc32_u32((uint32_t)initial_hash, *begin);

    // Iterate over consecutive u64 values
    while (iter64 < end64)
        initial_hash = _mm_crc32_u64((uint64_t)initial_hash, *iter64++);

    // If there is a 32-bit remainder, accumulate that
    if ((uint32_t*)iter64 < end)
        initial_hash = _mm_crc32_u32((uint32_t)initial_hash, *(uint32_t*)iter64);
#else
    // An inexpensive hash for CPUs lacking SSE4.2
    for (const uint32_t* iter = begin; iter < end; ++iter)
        initial_hash = 16777619U * initial_hash ^ *iter;
#endif

    return initial_hash;
}

template <class T>
uint64_t sse_hash_type(T const* value, uint32_t num_values = 1, uint64_t initial_hash = 2166136261U)
{
    static_assert((sizeof(T) & 3) == 0 && alignof(T) >= 4, "type is not word-aligned");
    return sse_hash((uint32_t*)value, (uint32_t*)(value + num_values), initial_hash);
}
}
