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
