#pragma once

#include <cstddef>

namespace phi::util
{
// From D3D12 sample MiniEngine
// https://github.com/Microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Math/Common.h
template <class T>
constexpr T align_up_masked(T value, size_t mask)
{
    return (T)(((size_t)value + mask) & ~mask);
}

template <class T>
[[nodiscard]] T align_down_masked(T value, size_t mask)
{
    return (T)((size_t)value & ~mask);
}

template <class T>
constexpr T align_up(T value, size_t alignment)
{
    return align_up_masked(value, alignment - 1);
}

template <class T>
[[nodiscard]] T align_down(T value, size_t alignment)
{
    return align_down_masked(value, alignment - 1);
}

template <class T>
[[nodiscard]] bool is_aligned(T value, size_t alignment)
{
    return 0 == ((size_t)value & (alignment - 1));
}

template <class T>
[[nodiscard]] T divide_by_multiple(T value, size_t alignment)
{
    return (T)((value + alignment - 1) / alignment);
}

template <class T>
[[nodiscard]] T align_offset(T offset, size_t alignment)
{
    return ((offset + (alignment - 1)) & ~(alignment - 1));
}
}
