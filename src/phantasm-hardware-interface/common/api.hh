#pragma once

#include <clean-core/macros.hh>

#ifdef CC_OS_WINDOWS

#ifdef PHI_BUILD_DLL

#ifdef PHI_DLL
#define PHI_API __declspec(dllexport)
#else
#define PHI_API __declspec(dllimport)
#endif

#else
#define PHI_API
#endif

#else
#define PHI_API
#endif
