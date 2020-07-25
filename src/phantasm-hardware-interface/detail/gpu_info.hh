#pragma once

#include <cstdint>

#include <clean-core/flags.hh>
#include <clean-core/span.hh>
#include <clean-core/string.hh>

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

// explicit GPU features
enum class gpu_feature : uint8_t
{
    raytracing,               ///< raytracing (tier 1 or higher)
    conservative_raster,      ///< conservative rasterization (tier 1 or higher)
    mesh_shaders,             ///< task/mesh shading pipeline (tier 1 or higher)
    rasterizer_ordered_views, ///< rasterizer ordered views (ROVs)
    shading_rate_t1,          ///< variable rate shading tier 1 or higher
    shading_rate_t2,          ///< variable rate shading tier 2 or higher
    hlsl_sm6,                 ///< shader model 6.0 or higher
    hlsl_wave_ops             ///< HLSL SM6 wave ops
};

using gpu_feature_flags = cc::flags<gpu_feature, 32>;

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

[[nodiscard]] size_t get_preferred_gpu(cc::span<gpu_info const> candidates, adapter_preference preference);

void print_startup_message(cc::span<gpu_info const> gpu_candidates, size_t chosen_index, backend_config const& config, bool is_d3d12);
}
