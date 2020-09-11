#include "util.hh"

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/format_size.hh>
#include <phantasm-hardware-interface/common/log.hh>

#include <clean-core/span.hh>

unsigned phi::util::get_texture_size_bytes(tg::isize3 size, phi::format fmt, int num_mips, bool is_d3d12)
{
    // calculate number of mips if zero is given
    num_mips = num_mips > 0 ? num_mips : get_num_mips(size.width, size.height);
    auto const bytes_per_pixel = get_format_size_bytes(fmt);
    auto res_bytes = 0u;

    for (auto mip = 0; mip < num_mips; ++mip)
    {
        auto const mip_width = get_mip_size(size.width, mip);
        auto const mip_height = get_mip_size(size.height, mip);

        unsigned row_pitch = bytes_per_pixel * mip_width;

        if (is_d3d12)
            row_pitch = phi::util::align_up(row_pitch, 256);

        auto const custom_offset = row_pitch * mip_height;
        res_bytes += custom_offset;
    }

    return res_bytes * size.depth;
}

unsigned phi::util::get_texture_pixel_byte_offset(tg::isize2 size, phi::format fmt, tg::ivec2 pixel, bool is_d3d12)
{
    CC_ASSERT(pixel.x < size.width && pixel.y < size.height && "pixel out of bounds");
    auto const bytes_per_pixel = get_format_size_bytes(fmt);

    unsigned row_width = bytes_per_pixel * size.width;

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
