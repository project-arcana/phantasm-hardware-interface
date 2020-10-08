#include "linked_pool.hh"

// Radix Sort implementation from https://github.com/983/RadixSort
// License: "Unlicense" (public domain)

namespace
{
CC_FORCE_INLINE void radix_sort_pass(uint32_t const* src, uint32_t* dst, size_t n, size_t shift)
{
    size_t next_index = 0, index[256] = {0};
    for (size_t i = 0; i < n; i++)
        index[(src[i] >> shift) & 0xff]++;
    for (size_t i = 0; i < 256; i++)
    {
        size_t const count = index[i];
        index[i] = next_index;
        next_index += count;
    }
    for (size_t i = 0; i < n; i++)
        dst[index[(src[i] >> shift) & 0xff]++] = src[i];
}
}

void phi::radix_sort(uint32_t* a, uint32_t* temp, size_t n)
{
    radix_sort_pass(a, temp, n, 0 * 8);
    radix_sort_pass(temp, a, n, 1 * 8);
    radix_sort_pass(a, temp, n, 2 * 8);
    radix_sort_pass(temp, a, n, 3 * 8);
}
