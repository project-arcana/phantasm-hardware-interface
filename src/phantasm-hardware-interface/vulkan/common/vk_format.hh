#pragma once


#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/common/format_info_list.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk::util
{
inline VkFormat to_vk_format(phi::format fmt)
{
    switch (fmt)
    {
        PHI_FORMAT_INFO_LIST_ALL(PHI_FORMAT_INFO_X_TO_VK)

    case format::none:
    case format::MAX_FORMAT_RANGE:
        return VK_FORMAT_UNDEFINED;
    }

    CC_UNREACHABLE("invalid format enum");
}

inline phi::format to_pr_format(VkFormat format)
{
    switch (format)
    {
        // only regular formats, view-only formats map to the same VkFormat in Vulkan
        PHI_FORMAT_INFO_LIST_REGULAR(PHI_FORMAT_INFO_X_FROM_VK)

    default:
        return format::none;
    }
}
}
