#pragma once

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace pr::backend::vk::util
{
[[nodiscard]] inline constexpr VkFormat to_vk_format(backend::format format)
{
    using bf = backend::format;
    switch (format)
    {
    case bf::rgba32f:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case bf::rgb32f:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case bf::rg32f:
        return VK_FORMAT_R32G32_SFLOAT;
    case bf::r32f:
        return VK_FORMAT_R32_SFLOAT;

    case bf::rgba32i:
        return VK_FORMAT_R32G32B32A32_SINT;
    case bf::rgb32i:
        return VK_FORMAT_R32G32B32_SINT;
    case bf::rg32i:
        return VK_FORMAT_R32G32_SINT;
    case bf::r32i:
        return VK_FORMAT_R32_SINT;

    case bf::rgba32u:
        return VK_FORMAT_R32G32B32A32_UINT;
    case bf::rgb32u:
        return VK_FORMAT_R32G32B32_UINT;
    case bf::rg32u:
        return VK_FORMAT_R32G32_UINT;
    case bf::r32u:
        return VK_FORMAT_R32_UINT;

    case bf::rgba16i:
        return VK_FORMAT_R16G16B16A16_SINT;
    case bf::rg16i:
        return VK_FORMAT_R16G16_SINT;
    case bf::r16i:
        return VK_FORMAT_R16_SINT;

    case bf::rgba16u:
        return VK_FORMAT_R16G16B16A16_UINT;
    case bf::rg16u:
        return VK_FORMAT_R16G16_UINT;
    case bf::r16u:
        return VK_FORMAT_R16_UINT;

    case bf::rgba16f:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case bf::rg16f:
        return VK_FORMAT_R16G16_SFLOAT;
    case bf::r16f:
        return VK_FORMAT_R16_SFLOAT;

    case bf::rgba8i:
        return VK_FORMAT_R8G8B8A8_SINT;
    case bf::rg8i:
        return VK_FORMAT_R8G8_SINT;
    case bf::r8i:
        return VK_FORMAT_R8_SINT;

    case bf::rgba8u:
        return VK_FORMAT_R8G8B8A8_UINT;
    case bf::rg8u:
        return VK_FORMAT_R8G8_UINT;
    case bf::r8u:
        return VK_FORMAT_R8_UINT;

    case bf::rgba8un:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case bf::rg8un:
        return VK_FORMAT_R8G8_UNORM;
    case bf::r8un:
        return VK_FORMAT_R8_UNORM;

    case bf::bgra8un:
        return VK_FORMAT_B8G8R8A8_UNORM;

    case bf::depth32f:
        return VK_FORMAT_D32_SFLOAT;
    case bf::depth16un:
        return VK_FORMAT_D16_UNORM;
    case bf::depth32f_stencil8u:
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case bf::depth24un_stencil8u:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    }
    CC_ASSERT(false && "unknown format");
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] inline constexpr pr::backend::format to_pr_format(VkFormat format)
{
    using bf = backend::format;
    switch (format)
    {
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return bf::rgba32f;
    case VK_FORMAT_R32G32B32_SFLOAT:
        return bf::rgb32f;
    case VK_FORMAT_R32G32_SFLOAT:
        return bf::rg32f;
    case VK_FORMAT_R32_SFLOAT:
        return bf::r32f;

    case VK_FORMAT_R32G32B32A32_SINT:
        return bf::rgba32i;
    case VK_FORMAT_R32G32B32_SINT:
        return bf::rgb32i;
    case VK_FORMAT_R32G32_SINT:
        return bf::rg32i;
    case VK_FORMAT_R32_SINT:
        return bf::r32i;

    case VK_FORMAT_R32G32B32A32_UINT:
        return bf::rgba32u;
    case VK_FORMAT_R32G32B32_UINT:
        return bf::rgb32u;
    case VK_FORMAT_R32G32_UINT:
        return bf::rg32u;
    case VK_FORMAT_R32_UINT:
        return bf::r32u;

    case VK_FORMAT_R16G16B16A16_SINT:
        return bf::rgba16i;
    case VK_FORMAT_R16G16_SINT:
        return bf::rg16i;
    case VK_FORMAT_R16_SINT:
        return bf::r16i;

    case VK_FORMAT_R16G16B16A16_UINT:
        return bf::rgba16u;
    case VK_FORMAT_R16G16_UINT:
        return bf::rg16u;
    case VK_FORMAT_R16_UINT:
        return bf::r16u;

    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return bf::rgba16f;
    case VK_FORMAT_R16G16_SFLOAT:
        return bf::rg16f;
    case VK_FORMAT_R16_SFLOAT:
        return bf::r16f;

    case VK_FORMAT_R8G8B8A8_SINT:
        return bf::rgba8i;
    case VK_FORMAT_R8G8_SINT:
        return bf::rg8i;
    case VK_FORMAT_R8_SINT:
        return bf::r8i;

    case VK_FORMAT_R8G8B8A8_UINT:
        return bf::rgba8u;
    case VK_FORMAT_R8G8_UINT:
        return bf::rg8u;
    case VK_FORMAT_R8_UINT:
        return bf::r8u;

    case VK_FORMAT_R8G8B8A8_UNORM:
        return bf::rgba8un;
    case VK_FORMAT_R8G8_UNORM:
        return bf::rg8un;
    case VK_FORMAT_R8_UNORM:
        return bf::r8un;

    case VK_FORMAT_B8G8R8A8_UNORM:
        return bf::bgra8un;

    case VK_FORMAT_D32_SFLOAT:
        return bf::depth32f;
    case VK_FORMAT_D16_UNORM:
        return bf::depth16un;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return bf::depth32f_stencil8u;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return bf::depth24un_stencil8u;

    default:
        CC_ASSERT(false && "untranslatable VkFormat");
        return bf::rgba8u;
    }
}
}
