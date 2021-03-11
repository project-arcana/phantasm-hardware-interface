#pragma once

#include <cstddef>
#include <cstdint>

namespace phi::util
{
// From D3D12 sample MiniEngine
// https://github.com/Microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Math/Common.h
template <class T>
[[nodiscard]] constexpr T align_up_masked(T value, size_t mask)
{
    return (T)(((size_t)value + mask) & ~mask);
}

template <class T>
[[nodiscard]] constexpr T align_down_masked(T value, size_t mask)
{
    return (T)((size_t)value & ~mask);
}

template <class T>
[[nodiscard]] constexpr T align_up(T value, size_t alignment)
{
    return align_up_masked(value, alignment - 1);
}

template <class T>
[[nodiscard]] constexpr T align_down(T value, size_t alignment)
{
    return align_down_masked(value, alignment - 1);
}

template <class T>
[[nodiscard]] constexpr bool is_aligned(T value, size_t alignment)
{
    return 0 == ((size_t)value & (alignment - 1));
}

template <class T>
[[nodiscard]] constexpr T divide_by_multiple(T value, size_t alignment)
{
    return (T)((value + alignment - 1) / alignment);
}

template <class T>
[[nodiscard]] constexpr T align_offset(T offset, size_t alignment)
{
    return ((offset + (alignment - 1)) & ~(alignment - 1));
}

inline uint32_t pack_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint32_t res = 0;
    res |= uint32_t(a) << 0;
    res |= uint32_t(b) << 8;
    res |= uint32_t(g) << 16;
    res |= uint32_t(r) << 24;
    return res;
}

struct unpacked_rgba_t
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

inline unpacked_rgba_t unpack_rgba8(uint32_t value)
{
    unpacked_rgba_t res;
    res.a = uint8_t((value >> 0) & 0xFF);
    res.b = uint8_t((value >> 8) & 0xFF);
    res.g = uint8_t((value >> 16) & 0xFF);
    res.r = uint8_t((value >> 24) & 0xFF);
    return res;
}
}
