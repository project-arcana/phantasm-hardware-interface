#pragma once

#include <cstdint>

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/fwd.hh>

namespace phi
{
enum class gpu_vendor
{
    INVALID = 0,

    amd,
    intel,
    nvidia,
    imgtec,
    arm,
    qualcomm,
    unknown
};

struct gpu_info
{
    char name[256] = {};

    // index into API-specific ordering
    uint32_t index = 0;

    // vendor based on PCIe ID
    gpu_vendor vendor = gpu_vendor::INVALID;

    // VRAM / shared memory
    size_t dedicated_video_memory_bytes = 0;
    size_t dedicated_system_memory_bytes = 0;
    size_t shared_system_memory_bytes = 0;
};

gpu_vendor getGPUVendorFromPCIeID(unsigned vendor_id);

size_t getPreferredGPU(cc::span<gpu_info const> candidates, adapter_preference preference);

void printStartupMessage(size_t numCandidates, gpu_info const* chosenCandidate, backend_config const& config, bool is_d3d12);
} // namespace phi
