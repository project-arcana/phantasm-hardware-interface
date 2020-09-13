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
    conservative_raster,      ///< conservative rasterization (>= tier 1)
    mesh_shaders,             ///< task/mesh shading pipeline (>= tier 1)
    rasterizer_ordered_views, ///< rasterizer ordered views (ROVs)
    hlsl_wave_ops             ///< HLSL SM6 wave ops
};

using gpu_feature_flags = cc::flags<gpu_feature, 32>;

struct gpu_feature_info
{
    enum e_hlsl_shader_model_version : uint8_t
    {
        hlsl_sm5_1, ///< shader model >= 5.1
        hlsl_sm6_0, ///< shader model >= 6.0
        hlsl_sm6_1, ///< shader model >= 6.1
        hlsl_sm6_2, ///< shader model >= 6.2
        hlsl_sm6_3, ///< shader model >= 6.3
        hlsl_sm6_4, ///< shader model >= 6.4
        hlsl_sm6_5, ///< shader model >= 6.5
        hlsl_sm6_6, ///< shader model >= 6.6
    };

    enum e_raytracing_tier : uint8_t
    {
        raytracing_unsupported,
        raytracing_t1_0,
        raytracing_t1_1
    };

    enum e_variable_rate_shading_tier : uint8_t
    {
        variable_rate_shading_unsupported,
        variable_rate_shading_t1_0,
        variable_rate_shading_t2_0
    };

    cc::flags<gpu_feature, 32> features = cc::no_flags;
    e_hlsl_shader_model_version sm_version = hlsl_sm5_1;
    e_raytracing_tier raytracing = raytracing_unsupported;
    e_variable_rate_shading_tier variable_rate_shading = variable_rate_shading_unsupported;
};

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
