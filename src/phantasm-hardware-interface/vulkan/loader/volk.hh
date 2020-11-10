#pragma once

#include "detail/platform_defines.hh"
#include "detail/volk.h"

#ifndef VK_VERSION_1_2
#error "[phantasm-hardware-interface] Vulkan SDK version 1.2 or higher required"
#endif

namespace phi::vk::vkver
{
enum vkver_e
{
#ifdef VK_HEADER_VERSION_COMPLETE
    major = VK_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE),
    minor = VK_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE),
    patch = VK_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE),
#else
    // older SDKs do not have the full version macro
    major = 1,
    minor =
#ifdef VK_API_VERSION_1_2
        2,
#elif defined(VK_API_VERSION_1_1)
        1,
#else
        0,
#endif
    patch = VK_HEADER_VERSION
#endif
};

}
