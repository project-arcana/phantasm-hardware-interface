#pragma once

#include <cstdint>

#include <clean-core/flags.hh>

namespace phi::d3d12
{
// TODO(25-05-23): Port a version of this to the global phi namespace and make it available via Backend + Add support in VK
// Expose flags like these (with explanations on what consequences the features have in phi API terms, or where to find them)
// Supersede 'isMeshShadingEnabled' and 'isRaytracingEnabled' with that new feature
#if 0
using gpu_feature_flags_t = uint32_t;
struct gpu_feature_flags
{
    enum : gpu_feature_flags_t
    {
        none = 0,

        // Hardware Raytracing, including the entire related API is available
        raytracing = 1 << 0,

        // Mesh shading is available (Mesh pipeline_state, cmd::dispatch_mesh, cmd::dispatch_mesh_indirect)
        mesh_shaders = 1 << 1,

        // Conservative rasterization is available (arg::pipeline_config::conservative_raster)
        conservative_raster = 1 << 2,

        // Rasterizer Ordered Views (ROVs) are available
        rasterizer_ordered_views = 1 << 3,

        // HLSL SM6 Wave Ops are available in shaders
        hlsl_wave_ops = 1 << 4,

        // HLSL SM6.6 ResourceDescriptorHeap/SamplerDescriptorHeap is usable
        hlsl_dynamic_resources = 1 << 5,
        
        // format::r9g9b9e5_sharedexp_uf can be used for UAVs and render targets
        rgb9e5_rt_uav = 1 << 6,
    };
};
#endif

// explicit GPU features
enum class gpu_feature : uint8_t
{
    conservative_raster,      ///< conservative rasterization (>= tier 1)
    mesh_shaders,             ///< task/mesh shading pipeline (>= tier 1)
    rasterizer_ordered_views, ///< rasterizer ordered views (ROVs)
    hlsl_wave_ops,            ///< HLSL SM6 wave ops
    hlsl_dynamic_resources,   ///< HLSL SM6.6 ResourceDescriptorHeap/SamplerDescriptorHeap
    rgb9e5_rt_uav,            ///< RGB9E5 as UAV and render target format
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
        hlsl_sm6_7, ///< shader model >= 6.7
        hlsl_sm6_8, ///< shader model >= 6.8
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
} // namespace phi::d3d12