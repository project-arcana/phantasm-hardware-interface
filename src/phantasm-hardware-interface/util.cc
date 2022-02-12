#include "util.hh"

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/format_size.hh>
#include <phantasm-hardware-interface/common/log.hh>

#include <clean-core/span.hh>

uint32_t phi::util::get_texture_size_bytes(tg::isize3 size, phi::format fmt, int num_mips, bool is_d3d12)
{
    // calculate number of mips if zero is given
    num_mips = num_mips > 0 ? num_mips : get_num_mips(size.width, size.height);
    auto const bytes_per_pixel = get_format_size_bytes(fmt);
    auto res_bytes = 0u;

    for (auto mip = 0; mip < num_mips; ++mip)
    {
        auto const mip_width = get_mip_size(size.width, mip);
        auto const mip_height = get_mip_size(size.height, mip);

        uint32_t row_pitch = bytes_per_pixel * mip_width;

        if (is_d3d12)
            row_pitch = phi::util::align_up(row_pitch, 256);

        auto const custom_offset = row_pitch * mip_height;
        res_bytes += custom_offset;
    }

    return res_bytes * size.depth;
}

uint32_t phi::util::get_texture_pixel_byte_offset(tg::isize2 size, phi::format fmt, tg::ivec2 pixel, bool is_d3d12)
{
    CC_ASSERT(pixel.x < size.width && pixel.y < size.height && "pixel out of bounds");
    auto const bytes_per_pixel = get_format_size_bytes(fmt);

    uint32_t row_width = bytes_per_pixel * size.width;

    if (is_d3d12)
        row_width = phi::util::align_up(row_width, 256);

    return pixel.y * row_width + pixel.x * bytes_per_pixel;
}

bool phi::util::is_rowwise_texture_data_copy_in_bounds(uint32_t dest_row_stride_bytes, uint32_t row_size_bytes, uint32_t num_rows, uint32_t source_size_bytes, uint32_t destination_size_bytes)
{
    // num_rows is the height in pixels for regular formats, but is lower for block compressed formats
    auto const largest_src_access = num_rows * row_size_bytes;
    auto const largest_dest_access = (num_rows - 1) * dest_row_stride_bytes + row_size_bytes;

    bool const is_in_bounds = (largest_src_access <= source_size_bytes) && (largest_dest_access <= destination_size_bytes);

    if (!is_in_bounds)
    {
        PHI_LOG_WARN("rowwise copy from texture data to upload buffer is out of bounds");
        if (largest_src_access > source_size_bytes)
            PHI_LOG_WARN("src bound error: access {} > size {} (exceeding by {} B)", largest_src_access, source_size_bytes, largest_src_access - source_size_bytes);
        if (largest_dest_access > destination_size_bytes)
            PHI_LOG_WARN("dst bound error: access {} > size {} (exceeding by {} B)", largest_dest_access, destination_size_bytes,
                         largest_dest_access - destination_size_bytes);

        PHI_LOG_WARN("while writing {} rows of {} bytes (strided {})", num_rows, row_size_bytes, dest_row_stride_bytes);
    }

    return is_in_bounds;
}

uint32_t phi::util::copy_texture_data_rowwise(void const* __restrict srcArg, void* __restrict destArg, uint32_t dest_row_stride_bytes, uint32_t row_size_bytes, uint32_t num_rows)
{
    CC_ASSERT(srcArg && destArg && row_size_bytes > 0 && dest_row_stride_bytes > 0);
    CC_ASSUME(srcArg && destArg);

    std::byte const* src = static_cast<std::byte const*>(srcArg);
    std::byte* dest = static_cast<std::byte *>(destArg);

    // num_rows is the height in pixels for regular formats, but is lower for block compressed formats
    for (auto y = 0u; y < num_rows; ++y)
    {
        auto const src_offset = y * row_size_bytes;
        auto const dst_offset = y * dest_row_stride_bytes;

        // TODO: streaming memcpy (destination is not immediately accessed)
        std::memcpy(dest + dst_offset, src + src_offset, row_size_bytes);
    }

    return dest_row_stride_bytes * (num_rows - 1) + row_size_bytes;
}

void phi::util::unswizzle_bgra_texture_data(cc::span<std::byte> in_out_texture_data)
{
    for (auto i = 0u; i < in_out_texture_data.size(); i += 4)
    {
        cc::swap(in_out_texture_data[i], in_out_texture_data[i + 2]);
    }
}

void phi::util::shader_table_offsets::init(const phi::shader_table_strides& record_strides,
                                           uint32_t num_ray_gen_stacks,
                                           uint32_t num_miss_stacks,
                                           uint32_t num_hit_group_stacks,
                                           uint32_t num_callable_stacks)
{
    this->strides = record_strides;
    this->num_ray_gen_stacks = num_ray_gen_stacks;
    this->num_miss_stacks = num_miss_stacks;
    this->num_hit_group_stacks = num_hit_group_stacks;
    this->num_callable_stacks = num_callable_stacks;

    offset_ray_gen_base = 0;
    auto const ray_gen_full_stack_size = num_ray_gen_stacks * align_up(record_strides.size_ray_gen, 64);

    offset_miss_base = ray_gen_full_stack_size;
    auto const miss_full_stack_size = num_miss_stacks * align_up(record_strides.size_miss, 64);

    offset_hit_group_base = offset_miss_base + miss_full_stack_size;
    auto const hit_group_full_stack_size = num_hit_group_stacks * align_up(record_strides.size_hit_group, 64);

    offset_callable_base = offset_hit_group_base + hit_group_full_stack_size;
    auto const callable_full_stack_size = num_callable_stacks * align_up(record_strides.size_callable, 64);

    total_size = offset_callable_base + callable_full_stack_size;
}
