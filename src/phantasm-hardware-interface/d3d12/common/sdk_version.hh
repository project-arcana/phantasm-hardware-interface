#pragma once

#include <sdkddkver.h>

// Mesh and Amplification shaders were added in Win10 20H1, also known as Win10 2004, codename Vibranium (Vb) (Released May 2020)
// NOTE: Explicitly do not use NTDDI_WIN10_VB on the right hand as it isn't defined on previous versions
#if WDK_NTDDI_VERSION >= 0x0A000008
#define PHI_D3D12_HAS_20H1_FEATURES 1
#else
#define PHI_D3D12_HAS_20H1_FEATURES 0
#endif
