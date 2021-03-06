#pragma once

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/common/format_info_list.hh>

namespace phi::util
{
/// returns the byte size of a single pixel of a texture in the given format
/// NOTE: block-compressed formats do not have a per-pixel size, use get_block_format_4x4_size for them instead
inline unsigned get_format_size_bytes(format fmt)
{
    unsigned res = 0;
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST_ALL(PHI_FORMAT_INFO_X_PIXELSIZE)
    default:
        CC_UNREACHABLE("unknown format");
        break;
    }
    CC_ASSERT(res > 0 && "compressed block formats have no per-pixel byte size, use get_block_format_4x4_size");
    return res;
}

/// returns the amount of components of a format (ie. RGBA = 4, Depth-Stencil = 2)
inline unsigned get_format_num_components(format fmt)
{
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST_ALL(PHI_FORMAT_INFO_X_NUM_COMPS)
    default:
        CC_UNREACHABLE("unknown format");
        return 0;
    }
}

/// returns the byte size of a 4x4 pixel square of a texture in the given block-compressed format
inline unsigned get_block_format_4x4_size(format fmt)
{
    switch (fmt)
    {
    case format::bc1:
    case format::bc1_srgb:
        // BC1 and BC4 cost 8 B per 4x4 pixels
        return 8;
    case format::bc2:
    case format::bc2_srgb:
    case format::bc3:
    case format::bc3_srgb:
    case format::bc6h_16f:
    case format::bc6h_16uf:
    case format::bc7:
    case format::bc7_srgb:
        // BC2, 3, 5, 6H and 7 cost 16 B per 4x4 pixels
        return 16;

    default:
        CC_UNREACHABLE("not a block-compressed format");
        return 0;
    }
}

/// returns the format's sRGB variant if existing, or the format itself otherwise
inline format get_format_srgb_variant(format fmt)
{
    switch (fmt)
    {
    case format::rgba8un:
        return format::rgba8un_srgb;
    case format::bc1:
        return format::bc1_srgb;
    case format::bc2:
        return format::bc2_srgb;
    case format::bc3:
        return format::bc3_srgb;
    case format::bc7:
        return format::bc7_srgb;
    default:
        // either fmt is already sRGB or no variant exsits
        return fmt;
    }
}

/// returns true if the format is a view-only format
constexpr bool is_view_format(format fmt)
{
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST_VIEWONLY(PHI_FORMAT_INFO_X_RET_TRUE)

    default:
        return false;
    }
}

/// returns true if the format is a block-compressed format
inline bool is_block_compressed_format(format fmt)
{
    using namespace phi::format_property_flags;
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST_ALL(PHI_FORMAT_INFO_X_IS_BLOCK_COMPRESSED)

    default:
        return false;
    }
}

/// returns true if the format is a depth OR depth stencil format
inline bool is_depth_format(format fmt)
{
    using namespace phi::format_property_flags;
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST_ALL(PHI_FORMAT_INFO_X_HAS_DEPTH)

    default:
        return false;
    }
}

/// returns true if the format is a depth stencil format
inline bool is_depth_stencil_format(format fmt)
{
    using namespace phi::format_property_flags;
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST_ALL(PHI_FORMAT_INFO_X_HAS_DEPTH_STENCIL)

    default:
        return false;
    }
}

inline bool is_srgb_format(format fmt)
{
    using namespace phi::format_property_flags;
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST_ALL(PHI_FORMAT_INFO_X_IS_SRGB)

    default:
        return false;
    }
}
}
