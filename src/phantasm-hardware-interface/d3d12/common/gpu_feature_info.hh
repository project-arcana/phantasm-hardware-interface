#pragma once

#include <cstdint>

#include <clean-core/flags.hh>

namespace phi::d3d12
{
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
}