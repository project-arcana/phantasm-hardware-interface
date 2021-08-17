#pragma once

typedef struct _EXCEPTION_POINTERS* PEXCEPTION_POINTERS;

namespace phi::d3d12::detail
{
bool is_delay_load_exception(PEXCEPTION_POINTERS e);

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

template <class Ft, class Fe>
void perform_safe_seh_call(Ft&& f_try, Fe&& f_except)
{
    __try
    {
        f_try();
    }
    __except (is_delay_load_exception(static_cast<_EXCEPTION_POINTERS*>(_exception_info())))
    {
        f_except();
    }
}
} // namespace phi::d3d12::detail
