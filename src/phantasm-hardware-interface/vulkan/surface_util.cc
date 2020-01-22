#include "surface_util.hh"

#include <clean-core/array.hh>
#include <clean-core/bit_cast.hh>
#include <clean-core/macros.hh>

#include <phantasm-hardware-interface/window_handle.hh>

#include "common/verify.hh"
#include "loader/volk.hh"

#ifdef CC_OS_WINDOWS
#include <clean-core/native/win32_sanitized.hh>
#elif defined(CC_OS_LINUX)
#include <X11/Xlib.h>
#endif

#ifdef PHI_HAS_SDL2
#include <SDL2/SDL_vulkan.h>
#endif

namespace
{
#ifdef CC_OS_WINDOWS
constexpr cc::array<char const*, 2> gc_required_vulkan_extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
#elif defined(CC_OS_LINUX)
constexpr cc::array<char const*, 2> gc_required_vulkan_extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
#endif

}

VkSurfaceKHR phi::vk::create_platform_surface(VkInstance instance, const phi::window_handle& window_handle)
{
    VkSurfaceKHR res_surface = nullptr;


    if (window_handle.type == window_handle::wh_sdl)
    {
        // SDL2 mode
#ifdef PHI_HAS_SDL2
        SDL_Vulkan_CreateSurface(static_cast<::SDL_Window*>(window_handle.value.sdl_handle), instance, &res_surface);
#else
        CC_RUNTIME_ASSERT(false && "SDL handle given, but compiled without SDL present");
#endif
    }
    else if (window_handle.type == window_handle::wh_win32_hwnd)
    {
        // Native HWND mode
#ifdef CC_OS_WINDOWS
        VkWin32SurfaceCreateInfoKHR surface_info = {};
        surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface_info.hwnd = window_handle.value.win32_hwnd;
        surface_info.hinstance = GetModuleHandle(nullptr);
        PR_VK_VERIFY_SUCCESS(vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &res_surface));
#else
        CC_RUNTIME_ASSERT(false && "Win32 HWND given, but compiled on non-win32 platform");
#endif
    }
    else if (window_handle.type == window_handle::wh_xlib)
    {
        // XLib mode
#ifdef CC_OS_LINUX
        VkXlibSurfaceCreateInfoKHR surface_info = {};
        surface_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        surface_info.dpy = window_handle.value.xlib_handles.display;
        surface_info.window = window_handle.value.xlib_handles.window;
        surface_info.pNext = nullptr;
        surface_info.flags = 0;
        PR_VK_VERIFY_SUCCESS(vkCreateXlibSurfaceKHR(instance, &surface_info, nullptr, &res_surface));
#else
        CC_RUNTIME_ASSERT(false && "Xlib handle given, but compiled on non-linux platform");
#endif
    }
    else
    {
        CC_RUNTIME_ASSERT(false && "unimplemented window handle type");
    }

    return res_surface;
}

cc::span<const char* const> phi::vk::get_platform_instance_extensions() { return gc_required_vulkan_extensions; }
