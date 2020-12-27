#pragma once

#ifndef DXGI_FORMAT_DEFINED // guard to allow for external use (ie. on Linux)
#include <dxgiformat.h>     // This header only contains a single enum, no includes believe it or not
#endif

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/common/format_info_list.hh>

namespace phi::d3d12::util
{
inline DXGI_FORMAT to_dxgi_format(phi::format fmt)
{
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST(PHI_FORMAT_INFO_X_TO_DXGI)
        PHI_FORMAT_INFO_LIST_VIEWONLY(PHI_FORMAT_INFO_X_TO_DXGI)

    case format::none:
    case format::MAX_FORMAT_RANGE:
        return DXGI_FORMAT_UNKNOWN;
    }

    CC_UNREACHABLE("invalid format enum");
}

inline phi::format to_pr_format(DXGI_FORMAT format)
{
    switch (format)
    {
        PHI_FORMAT_INFO_LIST(PHI_FORMAT_INFO_X_FROM_DXGI)
        PHI_FORMAT_INFO_LIST_VIEWONLY(PHI_FORMAT_INFO_X_FROM_DXGI)

    default:
        return format::none;
    }
}

/// Viewing some formats requires special DXGI_FORMATs
inline DXGI_FORMAT to_view_dxgi_format(phi::format format)
{
    using af = phi::format;
    switch (format)
    {
    case af::depth32f:
        return DXGI_FORMAT_R32_FLOAT;
    case af::depth16un:
        return DXGI_FORMAT_R16_UNORM;
    case af::depth32f_stencil8u:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case af::depth24un_stencil8u:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

    default:
        return to_dxgi_format(format);
    }
}
}
