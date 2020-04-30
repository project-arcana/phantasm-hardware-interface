#pragma once

#include <clean-core/bits.hh>
#include <clean-core/utility.hh>

namespace phi::util
{
/// returns the size the given texture dimension has at the specified mip level
/// get_mip_size(1024, 3) == 128
constexpr unsigned get_mip_size(unsigned width_height, unsigned mip_level)
{
    unsigned res = unsigned(width_height / float(1 << mip_level));
    return res > 0 ? res : 1;
}

/// returns the amount of levels in a full mip chain for a texture of the given size
inline unsigned get_num_mips(unsigned width, unsigned height) { return cc::bit_log2(cc::max(width, height)) + 1u; }
}
