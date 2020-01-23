#include "renderdoc_loader.hh"

#include <clean-core/macros.hh>

#include <renderdoc_app/renderdoc_app.h>

#ifdef CC_OS_WINDOWS
#include <clean-core/native/win32_sanitized.hh>
#elif defined(CC_OS_LINUX)
#include <dlfcn.h>
#endif

RENDERDOC_API_1_4_0* phi::detail::load_renderdoc()
{
    RENDERDOC_API_1_4_0* res = nullptr;

#ifdef CC_OS_WINDOWS
    if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_3_0, reinterpret_cast<void**>(&res));

        if (ret != 1)
            res = nullptr;
    }
#elif defined(CC_OS_LINUX)
    if (void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(mod, "RENDERDOC_GetAPI"));
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_3_0, reinterpret_cast<void**>(&res));

        if (ret != 1)
            res = nullptr;
    }
#else
    static_assert(false, "Unimplemented platform");
#endif

    return res;
}
