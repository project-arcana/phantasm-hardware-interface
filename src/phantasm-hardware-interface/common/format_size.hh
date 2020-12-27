#pragma once

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/common/format_info_list.hh>

namespace phi::util
{
inline unsigned get_format_size_bytes(format fmt)
{
    unsigned res = 0;
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST(PHI_FORMAT_INFO_X_PIXELSIZE)
        PHI_FORMAT_INFO_LIST_VIEWONLY(PHI_FORMAT_INFO_X_PIXELSIZE)
    default:
        CC_UNREACHABLE("unknown format");
        break;
    }
    CC_ASSERT(res > 0 && "compressed block formats have no per-pixel byte size, use get_block_format_4x4_size");
    return res;
}

inline unsigned get_format_num_components(format fmt)
{
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST(PHI_FORMAT_INFO_X_NUM_COMPS)
        PHI_FORMAT_INFO_LIST_VIEWONLY(PHI_FORMAT_INFO_X_NUM_COMPS)
    default:
        CC_UNREACHABLE("unknown format");
        return 0;
    }
}

inline unsigned get_block_format_4x4_size(format fmt)
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


inline format get_format_srgb_variant(format fmt)
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
