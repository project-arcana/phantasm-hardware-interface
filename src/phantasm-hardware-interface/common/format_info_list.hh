#pragma once

namespace phi::format_property_flags
{
enum format_property_flags_e
{
    efp_none = 0,
    efp_has_depth = 1 << 0,
    efp_has_stencil = 1 << 1,
    efp_is_srgb = 1 << 2,
    efp_is_bc = 1 << 3, // block compressed

    // test masks
    efp_has_depth_stencil = efp_has_depth | efp_has_stencil
};
}

// X(PhiName, NumComps, SizeBytes, Props, DxgiName, VkName, ...)
#define PHI_FORMAT_INFO_LIST_REGULAR(X)                                                                                      \
    X(rgba32f, 4, 16, efp_none, DXGI_FORMAT_R32G32B32A32_FLOAT, VK_FORMAT_R32G32B32A32_SFLOAT, 0x00)                         \
    X(rgb32f, 3, 12, efp_none, DXGI_FORMAT_R32G32B32_FLOAT, VK_FORMAT_R32G32B32_SFLOAT, 0x00)                                \
    X(rg32f, 2, 8, efp_none, DXGI_FORMAT_R32G32_FLOAT, VK_FORMAT_R32G32_SFLOAT, 0x00)                                        \
    X(r32f, 1, 4, efp_none, DXGI_FORMAT_R32_FLOAT, VK_FORMAT_R32_SFLOAT, 0x00)                                               \
    X(rgba32i, 4, 16, efp_none, DXGI_FORMAT_R32G32B32A32_SINT, VK_FORMAT_R32G32B32A32_SINT, 0x00)                            \
    X(rgb32i, 3, 12, efp_none, DXGI_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32_SINT, 0x00)                                   \
    X(rg32i, 2, 8, efp_none, DXGI_FORMAT_R32G32_SINT, VK_FORMAT_R32G32_SINT, 0x00)                                           \
    X(r32i, 1, 4, efp_none, DXGI_FORMAT_R32_SINT, VK_FORMAT_R32_SINT, 0x00)                                                  \
    X(rgba32u, 4, 16, efp_none, DXGI_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_UINT, 0x00)                            \
    X(rgb32u, 3, 12, efp_none, DXGI_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32_UINT, 0x00)                                   \
    X(rg32u, 2, 8, efp_none, DXGI_FORMAT_R32G32_UINT, VK_FORMAT_R32G32_UINT, 0x00)                                           \
    X(r32u, 1, 4, efp_none, DXGI_FORMAT_R32_UINT, VK_FORMAT_R32_UINT, 0x00)                                                  \
    X(rgba16i, 4, 8, efp_none, DXGI_FORMAT_R16G16B16A16_SINT, VK_FORMAT_R16G16B16A16_SINT, 0x00)                             \
    X(rg16i, 2, 4, efp_none, DXGI_FORMAT_R16G16_SINT, VK_FORMAT_R16G16_SINT, 0x00)                                           \
    X(r16i, 1, 2, efp_none, DXGI_FORMAT_R16_SINT, VK_FORMAT_R16_SINT, 0x00)                                                  \
    X(rgba16u, 4, 8, efp_none, DXGI_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_UINT, 0x00)                             \
    X(rg16u, 2, 4, efp_none, DXGI_FORMAT_R16G16_UINT, VK_FORMAT_R16G16_UINT, 0x00)                                           \
    X(r16u, 1, 2, efp_none, DXGI_FORMAT_R16_UINT, VK_FORMAT_R16_UINT, 0x00)                                                  \
    X(rgba16f, 4, 8, efp_none, DXGI_FORMAT_R16G16B16A16_FLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, 0x00)                          \
    X(rg16f, 2, 4, efp_none, DXGI_FORMAT_R16G16_FLOAT, VK_FORMAT_R16G16_SFLOAT, 0x00)                                        \
    X(r16f, 1, 2, efp_none, DXGI_FORMAT_R16_FLOAT, VK_FORMAT_R16_SFLOAT, 0x00)                                               \
    X(rgba8i, 4, 4, efp_none, DXGI_FORMAT_R8G8B8A8_SINT, VK_FORMAT_R8G8B8A8_SINT, 0x00)                                      \
    X(rg8i, 2, 2, efp_none, DXGI_FORMAT_R8G8_SINT, VK_FORMAT_R8G8_SINT, 0x00)                                                \
    X(r8i, 1, 1, efp_none, DXGI_FORMAT_R8_SINT, VK_FORMAT_R8_SINT, 0x00)                                                     \
    X(rgba8u, 4, 4, efp_none, DXGI_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R8G8B8A8_UINT, 0x00)                                      \
    X(rg8u, 2, 2, efp_none, DXGI_FORMAT_R8G8_UINT, VK_FORMAT_R8G8_UINT, 0x00)                                                \
    X(r8u, 1, 1, efp_none, DXGI_FORMAT_R8_UINT, VK_FORMAT_R8_UINT, 0x00)                                                     \
    X(rgba8un, 4, 4, efp_none, DXGI_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, 0x00)                                   \
    X(rg8un, 2, 2, efp_none, DXGI_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8_UNORM, 0x00)                                             \
    X(r8un, 1, 1, efp_none, DXGI_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM, 0x00)                                                  \
    X(rgba8un_srgb, 4, 4, efp_is_srgb, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, VK_FORMAT_R8G8B8A8_SRGB, 0x00)                       \
    X(bgra8un, 4, 4, efp_none, DXGI_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, 0x00)                                   \
    X(bgra4un, 4, 2, efp_none, DXGI_FORMAT_B4G4R4A4_UNORM, VK_FORMAT_B4G4R4A4_UNORM_PACK16, 0x00)                            \
    X(b10g11r11uf, 3, 4, efp_none, DXGI_FORMAT_R11G11B10_FLOAT, VK_FORMAT_B10G11R11_UFLOAT_PACK32, 0x00)                     \
    X(r10g10b10a2u, 4, 4, efp_none, DXGI_FORMAT_R10G10B10A2_UINT, VK_FORMAT_A2R10G10B10_UINT_PACK32, 0x00)                   \
    X(r10g10b10a2un, 4, 4, efp_none, DXGI_FORMAT_R10G10B10A2_UNORM, VK_FORMAT_A2R10G10B10_UNORM_PACK32, 0x00)                \
    X(b5g6r5un, 3, 2, efp_none, DXGI_FORMAT_B5G6R5_UNORM, VK_FORMAT_B5G6R5_UNORM_PACK16, 0x00)                               \
    X(b5g5r5a1un, 4, 2, efp_none, DXGI_FORMAT_B5G5R5A1_UNORM, VK_FORMAT_B5G5R5A1_UNORM_PACK16, 0x00)                         \
    X(r9g9b9e5_sharedexp_uf, 3, 4, efp_none, DXGI_FORMAT_R9G9B9E5_SHAREDEXP, VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, 0x00)         \
    X(bc1, 4, 0, efp_is_bc, DXGI_FORMAT_BC1_UNORM, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 0x00)                                     \
    X(bc1_srgb, 4, 0, efp_is_bc | efp_is_srgb, DXGI_FORMAT_BC1_UNORM_SRGB, VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 0x00)              \
    X(bc2, 4, 0, efp_is_bc, DXGI_FORMAT_BC2_UNORM, VK_FORMAT_BC2_UNORM_BLOCK, 0x00)                                          \
    X(bc2_srgb, 4, 0, efp_is_bc | efp_is_srgb, DXGI_FORMAT_BC2_UNORM_SRGB, VK_FORMAT_BC2_SRGB_BLOCK, 0x00)                   \
    X(bc3, 4, 0, efp_is_bc, DXGI_FORMAT_BC3_UNORM, VK_FORMAT_BC3_UNORM_BLOCK, 0x00)                                          \
    X(bc3_srgb, 4, 0, efp_is_bc | efp_is_srgb, DXGI_FORMAT_BC3_UNORM_SRGB, VK_FORMAT_BC3_SRGB_BLOCK, 0x00)                   \
    X(bc6h_16f, 3, 0, efp_is_bc, DXGI_FORMAT_BC6H_SF16, VK_FORMAT_BC6H_SFLOAT_BLOCK, 0x00)                                   \
    X(bc6h_16uf, 3, 0, efp_is_bc, DXGI_FORMAT_BC6H_UF16, VK_FORMAT_BC6H_UFLOAT_BLOCK, 0x00)                                  \
    X(bc7, 4, 0, efp_is_bc, DXGI_FORMAT_BC7_UNORM, VK_FORMAT_BC7_UNORM_BLOCK, 0x00)                                          \
    X(bc7_srgb, 4, 0, efp_is_bc | efp_is_srgb, DXGI_FORMAT_BC7_UNORM_SRGB, VK_FORMAT_BC7_SRGB_BLOCK, 0x00)                   \
    X(depth32f, 1, 4, efp_has_depth, DXGI_FORMAT_D32_FLOAT, VK_FORMAT_D32_SFLOAT, 0x00)                                      \
    X(depth16un, 1, 2, efp_has_depth, DXGI_FORMAT_D16_UNORM, VK_FORMAT_D16_UNORM, 0x00)                                      \
    X(depth32f_stencil8u, 2, 8, efp_has_depth_stencil, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, 0x00) \
    X(depth24un_stencil8u, 2, 4, efp_has_depth_stencil, DXGI_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, 0x00)

// view-only formats have a separate xmacro list, because they map to identical formats in vulkan
// (otherwise the Vulkan -> phi switch would have conflicting labels)
#define PHI_FORMAT_INFO_LIST_VIEWONLY(X)                                                               \
    X(r24un_g8t, 1, 0, efp_none, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, VK_FORMAT_D24_UNORM_S8_UINT, 0x00) \
    X(r24t_g8u, 1, 0, efp_none, DXGI_FORMAT_X24_TYPELESS_G8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, 0x00)

#define PHI_FORMAT_INFO_LIST_ALL(X) PHI_FORMAT_INFO_LIST_REGULAR(X) PHI_FORMAT_INFO_LIST_VIEWONLY(X)

//
// below: X functors to insert into the format lists

#define PHI_FORMAT_INFO_X_PIXELSIZE(PhiName, NumComps, SizeBytes, ...) \
    case format::PhiName:                                              \
        res = SizeBytes;                                               \
        break;

#define PHI_FORMAT_INFO_X_NUM_COMPS(PhiName, NumComps, ...) \
    case format::PhiName:                                   \
        return NumComps;

#define PHI_FORMAT_INFO_X_HAS_DEPTH(PhiName, NumComps, SizeBytes, Props, ...) \
    case format::PhiName:                                                     \
        return ((Props) & efp_has_depth) != 0;

#define PHI_FORMAT_INFO_X_HAS_DEPTH_STENCIL(PhiName, NumComps, SizeBytes, Props, ...) \
    case format::PhiName:                                                             \
        return ((Props) & efp_has_depth_stencil) == efp_has_depth_stencil;

#define PHI_FORMAT_INFO_X_IS_BLOCK_COMPRESSED(PhiName, NumComps, SizeBytes, Props, ...) \
    case format::PhiName:                                                               \
        return ((Props) & efp_is_bc) != 0;

#define PHI_FORMAT_INFO_X_IS_SRGB(PhiName, NumComps, SizeBytes, Props, ...) \
    case format::PhiName:                                                   \
        return ((Props) & efp_is_srgb) != 0;

#define PHI_FORMAT_INFO_X_TO_DXGI(PhiName, NumComps, SizeBytes, Props, DxgiName, ...) \
    case format::PhiName:                                                             \
        return DxgiName;

#define PHI_FORMAT_INFO_X_FROM_DXGI(PhiName, NumComps, SizeBytes, Props, DxgiName, ...) \
    case DxgiName:                                                                      \
        return format::PhiName;

#define PHI_FORMAT_INFO_X_TO_VK(PhiName, NumComps, SizeBytes, Props, DxgiName, VkName, ...) \
    case format::PhiName:                                                                   \
        return VkName;

#define PHI_FORMAT_INFO_X_FROM_VK(PhiName, NumComps, SizeBytes, Props, DxgiName, VkName, ...) \
    case VkName:                                                                              \
        return format::PhiName;

#define PHI_FORMAT_INFO_X_RET_TRUE(PhiName, ...) \
    case format::PhiName:                        \
        return true;
