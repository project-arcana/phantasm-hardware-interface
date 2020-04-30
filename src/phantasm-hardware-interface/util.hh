#pragma once

#include <clean-core/bits.hh>
#include <clean-core/utility.hh>

#include <typed-geometry/types/size.hh>

namespace phi::util
{
/// returns the size the given texture dimension has at the specified mip level
/// get_mip_size(1024, 3) == 128
constexpr int get_mip_size(int width_height, int mip_level)
{
    int res = int(width_height / float(1 << mip_level));
    return res > 0 ? res : 1;
}

constexpr tg::isize2 get_mip_size(tg::isize2 size, int mip_level)
{
    return {get_mip_size(size.width, mip_level), get_mip_size(size.height, mip_level)};
}

/// returns the amount of levels in a full mip chain for a texture of the given size
inline int get_num_mips(int width, int height) { return int(cc::bit_log2(cc::uint32(cc::max(width, height)))) + 1; }
inline int get_num_mips(tg::isize2 size) { return get_num_mips(size.width, size.height); }
}
