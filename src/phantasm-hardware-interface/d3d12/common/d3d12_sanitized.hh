#pragma once

// Check if CC win32 sanitized was included before this error
#ifdef CC_SANITIZED_WINDOWS_H
#error "This header is incompatbile with clean-core's sanitized win32"
#endif

// Check if <Windows.h> was included somewhere before this header
#if defined(_WINDOWS_) && !defined(PHI_SANITIZED_D3D12_H)
#error "Including unsanitized d3d12.h or Windows.h"
#endif
#define PHI_SANITIZED_D3D12_H

// clang-format off
#include <clean-core/native/detail/win32_sanitize_before.inl>

#include <Windows.h>

#ifdef PHI_HAS_D3D12_AGILITY
#include <D3D12AgilitySDK/d3d12.h>
#else
#include <d3d12.h>
#endif

#include <d3d11.h>
#include <d3d11on12.h>

#include <dxgi1_6.h>
#include <combaseapi.h>
#include <dxgidebug.h>
#include <comdef.h> // for _com_error(hr)

#include <clean-core/native/detail/win32_sanitize_after.inl>
// clang-format on
