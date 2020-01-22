#pragma once

typedef struct _EXCEPTION_POINTERS* PEXCEPTION_POINTERS;

namespace pr::backend::d3d12::detail
{
[[nodiscard]] bool is_delay_load_exception(PEXCEPTION_POINTERS e);

// Certain calls to DXGI can fail on some Win SDK versions (generally XP or lower) because the call is from a delay-loaded DLL,
// throwing a Win32 SEH exception. This helper performs such calls safely, using the special __try/__except syntax and an exception filter
template <class Ft>
void perform_safe_seh_call(Ft&& f_try)
{
    __try
    {
        f_try();
    }
    __except (is_delay_load_exception(static_cast<_EXCEPTION_POINTERS*>(_exception_info())))
    {
    }
}

}
