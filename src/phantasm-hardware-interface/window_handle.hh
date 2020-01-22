#pragma once

#include <cstdint>

struct SDL_Window;
typedef struct HWND__* HWND;
typedef struct _XDisplay Display;
typedef unsigned long XID;
typedef XID Window;

namespace pr::backend
{
/// opaque native window handle
struct window_handle
{
    enum wh_type : uint8_t
    {
        wh_sdl,
        wh_win32_hwnd,
        wh_xlib
    };

    wh_type type;

    union {
        ::SDL_Window* sdl_handle;
        ::HWND win32_hwnd;
        struct
        {
            ::Window window;
            ::Display* display;
        } xlib_handles;
    } value;

    window_handle(::HWND hwnd) : type(wh_win32_hwnd) { value.win32_hwnd = hwnd; }
    window_handle(::SDL_Window* sdl_window) : type(wh_sdl) { value.sdl_handle = sdl_window; }
    window_handle(::Window xlib_win, ::Display* xlib_display) : type(wh_xlib)
    {
        value.xlib_handles.window = xlib_win;
        value.xlib_handles.display = xlib_display;
    }
};
}
