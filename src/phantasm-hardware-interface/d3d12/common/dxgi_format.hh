#pragma once

#ifndef DXGI_FORMAT_DEFINED // guard to allow for external use (ie. on Linux)
#include <dxgiformat.h>     // This header only contains a single enum, no includes believe it or not
#endif

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/types.hh>

namespace phi::d3d12::util
{
[[nodiscard]] constexpr DXGI_FORMAT to_dxgi_format(phi::format format)
{
    using af = phi::format;
    switch (format)
    {
    case af::rgba32f:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case af::rgb32f:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case af::rg32f:
        return DXGI_FORMAT_R32G32_FLOAT;
    case af::r32f:
        return DXGI_FORMAT_R32_FLOAT;

    case af::rgba32i:
        return DXGI_FORMAT_R32G32B32A32_SINT;
    case af::rgb32i:
        return DXGI_FORMAT_R32G32B32_SINT;
    case af::rg32i:
        return DXGI_FORMAT_R32G32_SINT;
    case af::r32i:
        return DXGI_FORMAT_R32_SINT;

    case af::rgba32u:
        return DXGI_FORMAT_R32G32B32A32_UINT;
    case af::rgb32u:
        return DXGI_FORMAT_R32G32B32_UINT;
    case af::rg32u:
        return DXGI_FORMAT_R32G32_UINT;
    case af::r32u:
        return DXGI_FORMAT_R32_UINT;

    case af::rgba16i:
        return DXGI_FORMAT_R16G16B16A16_SINT;
    case af::rg16i:
        return DXGI_FORMAT_R16G16_SINT;
    case af::r16i:
        return DXGI_FORMAT_R16_SINT;

    case af::rgba16u:
        return DXGI_FORMAT_R16G16B16A16_UINT;
    case af::rg16u:
        return DXGI_FORMAT_R16G16_UINT;
    case af::r16u:
        return DXGI_FORMAT_R16_UINT;

    case af::rgba16f:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case af::rg16f:
        return DXGI_FORMAT_R16G16_FLOAT;
    case af::r16f:
        return DXGI_FORMAT_R16_FLOAT;

    case af::rgba8i:
        return DXGI_FORMAT_R8G8B8A8_SINT;
    case af::rg8i:
        return DXGI_FORMAT_R8G8_SINT;
    case af::r8i:
        return DXGI_FORMAT_R8_SINT;

    case af::rgba8u:
        return DXGI_FORMAT_R8G8B8A8_UINT;
    case af::rg8u:
        return DXGI_FORMAT_R8G8_UINT;
    case af::r8u:
        return DXGI_FORMAT_R8_UINT;

    case af::rgba8un:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case af::rg8un:
        return DXGI_FORMAT_R8G8_UNORM;
    case af::r8un:
        return DXGI_FORMAT_R8_UNORM;

    case af::rgba8un_srgb:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    case af::bgra8un:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case af::b10g11r11uf:
        return DXGI_FORMAT_R11G11B10_FLOAT; // this is incorrectly named, order is as in our enum

    case af::r10g10b10a2u:
        return DXGI_FORMAT_R10G10B10A2_UINT;
    case af::r10g10b10a2un:
        return DXGI_FORMAT_R10G10B10A2_UNORM;

    case af::bc1_8un:
        return DXGI_FORMAT_BC1_UNORM;
    case af::bc1_8un_srgb:
        return DXGI_FORMAT_BC1_UNORM_SRGB;
    case af::bc2_8un:
        return DXGI_FORMAT_BC2_UNORM;
    case af::bc2_8un_srgb:
        return DXGI_FORMAT_BC2_UNORM_SRGB;
    case af::bc3_8un:
        return DXGI_FORMAT_BC3_UNORM;
    case af::bc3_8un_srgb:
        return DXGI_FORMAT_BC3_UNORM_SRGB;
    case af::bc6h_16f:
        return DXGI_FORMAT_BC6H_SF16;
    case af::bc6h_16uf:
        return DXGI_FORMAT_BC6H_UF16;

    case af::r24un_g8t:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case af::r24t_g8u:
        return DXGI_FORMAT_X24_TYPELESS_G8_UINT;

    case af::depth32f:
        return DXGI_FORMAT_D32_FLOAT;
    case af::depth16un:
        return DXGI_FORMAT_D16_UNORM;
    case af::depth32f_stencil8u:
        return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case af::depth24un_stencil8u:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;

    case af::none:
    case af::MAX_FORMAT_RANGE:
        return DXGI_FORMAT_UNKNOWN;
    }
    CC_UNREACHABLE_SWITCH_WORKAROUND(format);
}

/// Viewing some formats requires special DXGI_FORMATs
[[nodiscard]] constexpr DXGI_FORMAT to_view_dxgi_format(phi::format format)
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

[[nodiscard]] constexpr phi::format to_pr_format(DXGI_FORMAT format)
{
    using af = phi::format;
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return af::rgba32f;
    case DXGI_FORMAT_R32G32B32_FLOAT:
        return af::rgb32f;
    case DXGI_FORMAT_R32G32_FLOAT:
        return af::rg32f;
    case DXGI_FORMAT_R32_FLOAT:
        return af::r32f;

    case DXGI_FORMAT_R32G32B32A32_SINT:
        return af::rgba32i;
    case DXGI_FORMAT_R32G32B32_SINT:
        return af::rgb32i;
    case DXGI_FORMAT_R32G32_SINT:
        return af::rg32i;
    case DXGI_FORMAT_R32_SINT:
        return af::r32i;

    case DXGI_FORMAT_R32G32B32A32_UINT:
        return af::rgba32u;
    case DXGI_FORMAT_R32G32B32_UINT:
        return af::rgb32u;
    case DXGI_FORMAT_R32G32_UINT:
        return af::rg32u;
    case DXGI_FORMAT_R32_UINT:
        return af::r32u;

    case DXGI_FORMAT_R16G16B16A16_SINT:
        return af::rgba16i;
    case DXGI_FORMAT_R16G16_SINT:
        return af::rg16i;
    case DXGI_FORMAT_R16_SINT:
        return af::r16i;

    case DXGI_FORMAT_R16G16B16A16_UINT:
        return af::rgba16u;
    case DXGI_FORMAT_R16G16_UINT:
        return af::rg16u;
    case DXGI_FORMAT_R16_UINT:
        return af::r16u;

    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return af::rgba16f;
    case DXGI_FORMAT_R16G16_FLOAT:
        return af::rg16f;
    case DXGI_FORMAT_R16_FLOAT:
        return af::r16f;

    case DXGI_FORMAT_R8G8B8A8_SINT:
        return af::rgba8i;
    case DXGI_FORMAT_R8G8_SINT:
        return af::rg8i;
    case DXGI_FORMAT_R8_SINT:
        return af::r8i;

    case DXGI_FORMAT_R8G8B8A8_UINT:
        return af::rgba8u;
    case DXGI_FORMAT_R8G8_UINT:
        return af::rg8u;
    case DXGI_FORMAT_R8_UINT:
        return af::r8u;

    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return af::rgba8un;
    case DXGI_FORMAT_R8G8_UNORM:
        return af::rg8un;
    case DXGI_FORMAT_R8_UNORM:
        return af::r8un;

        // sRGB formats
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return af::rgba8un_srgb;

        // swizzled and irregular formats
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return af::bgra8un;
    case DXGI_FORMAT_R11G11B10_FLOAT: // this is incorrectly named, order is as in our enum
        return af::b10g11r11uf;
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return af::r10g10b10a2u;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return af::r10g10b10a2un;

        // compressed formats
    case DXGI_FORMAT_BC1_UNORM:
        return af::bc1_8un;
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return af::bc1_8un_srgb;
    case DXGI_FORMAT_BC2_UNORM:
        return af::bc2_8un;
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        return af::bc2_8un_srgb;
    case DXGI_FORMAT_BC3_UNORM:
        return af::bc3_8un;
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return af::bc3_8un_srgb;
    case DXGI_FORMAT_BC6H_SF16:
        return af::bc6h_16f;
    case DXGI_FORMAT_BC6H_UF16:
        return af::bc6h_16uf;

        // depth formats
    case DXGI_FORMAT_D32_FLOAT:
        return af::depth32f;
    case DXGI_FORMAT_D16_UNORM:
        return af::depth16un;

        // depth stencil formats
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return af::depth32f_stencil8u;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return af::depth24un_stencil8u;

    default:
        CC_ASSERT(false && "untranslatable DXGI_FORMAT");
        return af::none;
    }
}
}
