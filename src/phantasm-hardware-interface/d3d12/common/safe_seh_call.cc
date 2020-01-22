#include "safe_seh_call.hh"

#include <clean-core/native/win32_sanitized.hh>

#include <delayimp.h>

bool pr::backend::d3d12::detail::is_delay_load_exception(PEXCEPTION_POINTERS e)
{
#if WINVER > 0x502
    switch (e->ExceptionRecord->ExceptionCode)
    {
    case VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND):
    case VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND):
        return EXCEPTION_EXECUTE_HANDLER;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
#else // Windows SDK 7.1 doesn't define VcppException
    return EXCEPTION_EXECUTE_HANDLER;
#endif
}

