#pragma once

#include <clean-core/macros.hh>

#if defined(CC_OS_WINDOWS) && defined(CC_TARGET_PC)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(CC_TARGET_XBOX)
#error "Vulkan not supported on Xbox"
#elif defined(CC_TARGET_ORBIS)
#error "Vulkan not supported on PS"
#elif defined(CC_TARGET_NX)
#error "Unimplemented platform"
#elif defined(CC_TARGET_ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(CC_OS_APPLE)

#if defined(CC_TARGET_MACOS)
#define VK_USE_PLATFORM_MACOS_MVK
#elif defined(CC_TARGET_IOS)
#define VK_USE_PLATFORM_IOS_MVK
#else
#error "Unsupported Apple platform"
#endif

#elif defined(CC_OS_LINUX)
#define VK_USE_PLATFORM_XLIB_KHR
// TODO: Wayland (VK_USE_PLATFORM_WAYLAND_KHR)
#else
#error "Unsupported and unknown platform for Vulkan backend"
#endif
