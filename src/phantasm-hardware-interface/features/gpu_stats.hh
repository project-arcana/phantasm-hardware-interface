#pragma once

#include <phantasm-hardware-interface/common/api.hh>

namespace phi::gpustats
{
using gpu_handle_t = void*;

// initialize and shut down GPU stat subsystem (loading NVML or AMD GPUinfo)
// everything but these two calls are threadsafe
PHI_API bool initialize();
PHI_API void shutdown();

/// retrieve a GPU handle by the device index, nullptr if invalid
PHI_API gpu_handle_t get_gpu_by_index(unsigned index);

/// returns the GPU die temperature in degrees celsius, -1 for invalid handles
PHI_API int get_temperature(gpu_handle_t handle);
/// returns the GPU fanspeed in percent, -1 for invalid handles
PHI_API int get_fanspeed_percent(gpu_handle_t handle);

}
