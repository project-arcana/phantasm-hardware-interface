#pragma once

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/types.hh>

namespace phi::util
{
[[nodiscard]] inline unsigned get_format_size_bytes(format fmt)
{
    switch (fmt)
    {
    case format::rgba32f:
    case format::rgba32i:
    case format::rgba32u:
        return 16;

    case format::rgb32f:
    case format::rgb32i:
    case format::rgb32u:
        return 12;

    case format::rg32f:
    case format::rg32i:
    case format::rg32u:
        return 8;

    case format::r32f:
    case format::r32i:
    case format::r32u:
    case format::depth32f:
        return 4;

    case format::rgba16f:
    case format::rgba16i:
    case format::rgba16u:
        return 8;

    case format::rg16f:
    case format::rg16i:
    case format::rg16u:
        return 4;

    case format::r16f:
    case format::r16i:
    case format::r16u:
    case format::depth16un:
        return 2;

    case format::rgba8i:
    case format::rgba8u:
    case format::rgba8un:
    case format::rgba8un_srgb:
    case format::bgra8un:
        return 4;

    case format::rg8i:
    case format::rg8u:
    case format::rg8un:
        return 2;

    case format::r8i:
    case format::r8u:
    case format::r8un:
        return 1;

    case format::depth32f_stencil8u:
        return 8;
    case format::depth24un_stencil8u:
    case format::r24t_g8u:
    case format::r24un_g8t:
        return 4;

    case format::b10g11r11uf:
    case format::r10g10b10a2u:
    case format::r10g10b10a2un:
        return 4;

        // NOTE: block-compressed formats do not have per-pixel byte sizes
        // they cost bytes per NxN square block
    case format::bc1_8un:
    case format::bc1_8un_srgb:
        // BC1 and BC4 cost 8 B per 4x4 pixels
    case format::bc2_8un:
    case format::bc2_8un_srgb:
    case format::bc3_8un:
    case format::bc3_8un_srgb:
    case format::bc6h_16f:
    case format::bc6h_16uf:
        // BC2, 3, 5, 6H and 7 cost 16 B per 4x4 pixels
        CC_UNREACHABLE("compressed block format has no per-pixel byte size");
        return 0;
    default:
        CC_UNREACHABLE("unknown format");
        return 0;
    }
    CC_UNREACHABLE("unknown format");
    return 0;
}

[[nodiscard]] inline unsigned get_block_format_4x4_size(format fmt)
{
    switch (fmt)
    {
    case format::bc1_8un:
    case format::bc1_8un_srgb:
        // BC1 and BC4 cost 8 B per 4x4 pixels
        return 8;
    case format::bc2_8un:
    case format::bc2_8un_srgb:
    case format::bc3_8un:
    case format::bc3_8un_srgb:
    case format::bc6h_16f:
    case format::bc6h_16uf:
        // BC2, 3, 5, 6H and 7 cost 16 B per 4x4 pixels
        return 16;

    default:
        CC_UNREACHABLE("not a block-compressed format");
        return 0;
    }
}

[[nodiscard]] inline unsigned get_format_num_components(format fmt)
{
    switch (fmt)
    {
    case format::rgba32f:
    case format::rgba32i:
    case format::rgba32u:
    case format::rgba16f:
    case format::rgba16i:
    case format::rgba16u:
    case format::rgba8i:
    case format::rgba8u:
    case format::rgba8un:
    case format::rgba8un_srgb:
    case format::bgra8un:
    case format::r10g10b10a2u:
    case format::r10g10b10a2un:
        return 4;

    case format::rgb32f:
    case format::rgb32i:
    case format::rgb32u:
    case format::b10g11r11uf:
    case format::bc6h_16f:
    case format::bc6h_16uf:
        return 3;

    case format::rg32f:
    case format::rg32i:
    case format::rg32u:
    case format::rg16f:
    case format::rg16i:
    case format::rg16u:
    case format::rg8i:
    case format::rg8u:
    case format::rg8un:
    case format::depth32f_stencil8u:
    case format::depth24un_stencil8u:
        return 2;

    case format::r32f:
    case format::r32i:
    case format::r32u:
    case format::depth32f:
    case format::r16f:
    case format::r16i:
    case format::r16u:
    case format::depth16un:
    case format::r8i:
    case format::r8u:
    case format::r8un:
    case format::r24t_g8u:
    case format::r24un_g8t:
        return 1;
    default:
        CC_UNREACHABLE("unknown format");
        return 0;
    }
    CC_UNREACHABLE("unknown format");
    return 0;
}

[[nodiscard]] inline format get_format_srgb_variant(format fmt)
{
    switch (fmt)
    {
    case format::rgba8un:
        return format::rgba8un_srgb;
    case format::bc1_8un:
        return format::bc1_8un_srgb;
    case format::bc2_8un:
        return format::bc2_8un_srgb;
    case format::bc3_8un:
        return format::bc3_8un_srgb;
    default:
        return fmt;
    }
}
}
