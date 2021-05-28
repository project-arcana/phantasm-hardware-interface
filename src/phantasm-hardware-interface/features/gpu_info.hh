#pragma once

#include <cstdint>

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/fwd.hh>

namespace phi
{
enum class gpu_vendor : uint8_t
{
    amd,
    intel,
    nvidia,
    imgtec,
    arm,
    qualcomm,

    unknown
};

// opaque, API-specific capability level, higher is better
enum class gpu_capabilities : uint8_t
{
    insufficient,
    level_1,
    level_2,
    level_3
};

struct gpu_info
{
    char name[256];
    unsigned index; ///< an index into an API-specific ordering

    size_t dedicated_video_memory_bytes;
    size_t dedicated_system_memory_bytes;
    size_t shared_system_memory_bytes;

    gpu_vendor vendor;
    gpu_capabilities capabilities;
    bool has_raytracing;
};

gpu_vendor getGPUVendorFromPCIeID(unsigned vendor_id);

size_t getPreferredGPU(cc::span<gpu_info const> candidates, adapter_preference preference);

void printStartupMessage(cc::span<gpu_info const> gpu_candidates, size_t chosen_index, backend_config const& config, bool is_d3d12);
} // namespace phi
