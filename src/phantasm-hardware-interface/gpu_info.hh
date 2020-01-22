#pragma once

#include <cstdint>

#include <clean-core/flags.hh>
#include <clean-core/span.hh>
#include <clean-core/string.hh>

#include "config.hh"

namespace pr::backend
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

// explicit GPU features
enum class gpu_feature : uint8_t
{
    raytracing,
    shading_rate_t1,
    shading_rate_t2,
    hlsl_sm6,
    hlsl_wave_ops
};

using gpu_feature_flags = cc::flags<gpu_feature, 16>;

struct gpu_info
{
    cc::string description;
    unsigned index; ///< an index into an API-specific ordering

    size_t dedicated_video_memory_bytes;
    size_t dedicated_system_memory_bytes;
    size_t shared_system_memory_bytes;

    gpu_vendor vendor;
    gpu_capabilities capabilities;
    bool has_raytracing;
};

[[nodiscard]] gpu_vendor get_gpu_vendor_from_id(unsigned vendor_id);

[[nodiscard]] size_t get_preferred_gpu(cc::span<gpu_info const> candidates, adapter_preference preference, bool verbose = true);
}
