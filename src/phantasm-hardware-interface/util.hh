#pragma once

#include <cstdint>

#include <clean-core/bits.hh>
#include <clean-core/utility.hh>

#include <typed-geometry/types/size.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/common/api.hh>
#include <phantasm-hardware-interface/common/byte_util.hh>

namespace phi::util
{
/// returns the size in pixels the given texture dimension has at the specified mip level
/// get_mip_size(1024, 3) == 128
constexpr int get_mip_size(int width_height, int mip_level)
{
    int res = int(width_height / float(1u << mip_level));
    return res > 0 ? res : 1;
}

constexpr tg::isize2 get_mip_size(tg::isize2 size, int mip_level)
{
    return {get_mip_size(size.width, mip_level), get_mip_size(size.height, mip_level)};
}

/// returns the amount of levels in a full mip chain for a texture of the given size
inline int get_num_mips(int width, int height) { return int(cc::bit_log2(uint32_t(cc::max(width, height)))) + 1; }
inline int get_num_mips(tg::isize2 size) { return get_num_mips(size.width, size.height); }

/// returns the amount of bytes needed to store the contents of a texture in a GPU buffer
PHI_API uint32_t get_texture_size_bytes(tg::isize3 size, format fmt, int num_mips, bool is_d3d12);

/// returns the offset in bytes of the given pixel position in a texture of given size and format (in a GPU buffer)
PHI_API uint32_t get_texture_pixel_byte_offset(tg::isize2 size, format fmt, tg::ivec2 pixel, bool is_d3d12);

/// converts texture data from bgra8 to rgba8
PHI_API void unswizzle_bgra_texture_data(cc::span<std::byte> in_out_texture_data);

/// returns the offset in bytes of the next element of size 'next_size_bytes' in a HLSL constant buffer
/// where head_offset_bytes is the amount of bytes already in use
constexpr uint32_t get_hlsl_constant_buffer_offset(uint32_t head_offset_bytes, uint32_t next_size_bytes)
{
    // ref: https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
    CC_ASSERT(next_size_bytes <= 16 && "unexpectedly large element");

    // head is always aligned up to a 4-byte boundary
    auto const head_aligned_4 = phi::util::align_up(head_offset_bytes, 4);
    if (cc::mod_pow2(head_aligned_4, 16) + next_size_bytes > 16)
    {
        // if the element would straddle a 16-byte boundary (float4), it is pushed into the next one
        return phi::util::align_up(head_offset_bytes, 16);
    }
    else
    {
        return head_aligned_4;
    }
}

/// returns the difference between two GPU timestamp values in milliseconds
/// timestamp_frequency can be obtained from Backend::getGPUTimestampFrequency
inline double get_timestamp_difference_milliseconds(uint64_t start, uint64_t end, uint64_t timestamp_frequency)
{
    return (double(end - start) / timestamp_frequency) * 1000.;
}

/// returns the difference between two GPU timestamp values in microseconds
/// timestamp_frequency can be obtained from Backend::getGPUTimestampFrequency
inline uint64_t get_timestamp_difference_microseconds(uint64_t start, uint64_t end, uint64_t timestamp_frequency)
{
    return (end - start) / (timestamp_frequency / 1'000'000);
}

struct PHI_API shader_table_offsets
{
    shader_table_strides strides;

    uint32_t num_ray_gen_stacks = 1;
    uint32_t num_miss_stacks = 1;
    uint32_t num_hit_group_stacks = 1;
    uint32_t num_callable_stacks = 1;

    uint32_t offset_ray_gen_base = 0;
    uint32_t offset_miss_base = 0;
    uint32_t offset_hit_group_base = 0;
    uint32_t offset_callable_base = 0;

    uint32_t total_size = 0;

    void init(shader_table_strides const& record_strides,
              uint32_t num_ray_gen_stacks = 1,
              uint32_t num_miss_stacks = 1,
              uint32_t num_hit_group_stacks = 1,
              uint32_t num_callable_stacks = 1);

    uint32_t get_ray_gen_offset(uint32_t stack_index = 0) const
    {
        CC_ASSERT(stack_index < num_ray_gen_stacks && "stack OOB");
        return offset_ray_gen_base + align_up(strides.size_ray_gen * stack_index, 64);
    }

    uint32_t get_miss_offset(uint32_t stack_index = 0) const
    {
        CC_ASSERT(stack_index < num_miss_stacks && "stack OOB");
        return offset_miss_base + align_up(strides.size_miss * stack_index, 64);
    }

    uint32_t get_hitgroup_offset(uint32_t stack_index = 0) const
    {
        CC_ASSERT(stack_index < num_hit_group_stacks && "stack OOB");
        return offset_hit_group_base + align_up(strides.size_hit_group * stack_index, 64);
    }

    uint32_t get_callable_offset(uint32_t stack_index = 0) const
    {
        CC_ASSERT(stack_index < num_callable_stacks && "stack OOB");
        return offset_callable_base + align_up(strides.size_callable * stack_index, 64);
    }
};
} // namespace phi::util
