#pragma once

#include <cstdint>

namespace phi::detail
{
template <class StateT>
struct generic_incomplete_state_cache;

template <class T, uint8_t N>
struct trivial_capped_vector;

template <class KeyT, class ValT>
struct flat_linear_map;
template <class KeyT, class ValT, size_t N>
struct capped_flat_map;
template <class KeyT, class ValueT, class HashT>
struct stable_map;

}

namespace phi
{
template <class T, bool GenCheckEnabled>
struct linked_pool;

struct page_allocator;
struct thread_association;
struct unique_buffer;
}
