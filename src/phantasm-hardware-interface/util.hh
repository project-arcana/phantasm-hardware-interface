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
// returns the size in pixels the given texture dimension has at the specified mip level
// get_mip_size(1024, 3) == 128
constexpr int get_mip_size(int width_height, int mip_level)
{
    int res = int(width_height / float(1u << mip_level));
    return res > 0 ? res : 1;
}

constexpr tg::isize2 get_mip_size(tg::isize2 size, int mip_level)
{
    return {get_mip_size(size.width, mip_level), get_mip_size(size.height, mip_level)};
}

// returns the amount of levels in a full mip chain for a texture of the given size
inline int get_num_mips(int width, int height) { return int(cc::bit_log2(uint32_t(cc::max(width, height)))) + 1; }
inline int get_num_mips(tg::isize2 size) { return get_num_mips(size.width, size.height); }

// computes byte size in a GPU buffer to store contents of a texture subresource
inline uint32_t get_texture_subresource_size_bytes(uint32_t row_size_bytes, uint32_t num_rows, bool is_d3d12)
{
    if (num_rows == 0)
    {
        return 0;
    }

    auto row_stride_bytes = is_d3d12 ? phi::util::align_up(row_size_bytes, 256u) : row_size_bytes;
    return row_stride_bytes * (num_rows - 1) + row_size_bytes;
}

// computes byte size in a GPU buffer to store contents of a texture
PHI_API uint32_t get_texture_size_bytes(tg::isize3 size, format fmt, int num_mips, bool is_d3d12);

// computes byte offset in a GPU buffer of the given pixel position in a texture
PHI_API uint32_t get_texture_pixel_byte_offset(tg::isize2 size, format fmt, tg::ivec2 pixel, bool is_d3d12);

// checks if a rowwise texture data copy is in bounds on both input and output memory
// logs a detailed warning if OOB
PHI_API bool is_rowwise_texture_data_copy_in_bounds(uint32_t dest_row_stride_bytes, uint32_t row_size_bytes, uint32_t num_rows, uint32_t source_size_bytes, uint32_t destination_size_bytes);

// copies input texture data to destination memory row-by-row, respecting row strides
// returns num bytes written to dest
PHI_API uint32_t copy_texture_data_rowwise(void const* __restrict src, void* __restrict dest, uint32_t dest_row_stride_bytes, uint32_t row_size_bytes, uint32_t num_rows);

// converts texture data from bgra8 to rgba8
PHI_API void unswizzle_bgra_texture_data(cc::span<std::byte> in_out_texture_data);

// returns the offset in bytes of the next element of size 'next_size_bytes' in a HLSL constant buffer
// where head_offset_bytes is the amount of bytes already in use
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

// returns the difference between two GPU timestamp values in milliseconds
// timestamp_frequency can be obtained from Backend::getGPUTimestampFrequency
inline double get_timestamp_difference_milliseconds(uint64_t start, uint64_t end, uint64_t timestamp_frequency)
{
    return (double(end - start) / timestamp_frequency) * 1000.;
}

// returns the difference between two GPU timestamp values in microseconds
// timestamp_frequency can be obtained from Backend::getGPUTimestampFrequency
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
