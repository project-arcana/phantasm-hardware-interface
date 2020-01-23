#pragma once

#include <clean-core/macros.hh>

#ifdef CC_OS_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(CC_OS_LINUX)
#define VK_USE_PLATFORM_XLIB_KHR
// TODO: Wayland (VK_USE_PLATFORM_WAYLAND_KHR)
#elif defined(CC_OS_OSX)
#define VK_USE_PLATFORM_MACOS_MVK
#elif defined(CC_OS_IOS)
#define VK_USE_PLATFORM_IOS_MVK
#else
#error "Unsupported Platform for Vulkan backend"
#endif
