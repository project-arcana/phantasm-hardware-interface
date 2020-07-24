#include "adapter_choice_util.hh"

#include <cstdio>

#include <sdkddkver.h>

#include <clean-core/array.hh>
#include <clean-core/assert.hh>

#include "common/safe_seh_call.hh"
#include "common/shared_com_ptr.hh"
#include "common/verify.hh"

int phi::d3d12::test_adapter(IDXGIAdapter* adapter, D3D_FEATURE_LEVEL min_features, D3D_FEATURE_LEVEL& out_max_features)
{
    cc::array const all_feature_levels = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};

    int res_nodes = -1;

    detail::perform_safe_seh_call([&] {
        shared_com_ptr<ID3D12Device> test_device;
        auto const hres = ::D3D12CreateDevice(adapter, min_features, PHI_COM_WRITE(test_device));

        if (SUCCEEDED(hres))
        {
            D3D12_FEATURE_DATA_FEATURE_LEVELS feature_data;
            feature_data.pFeatureLevelsRequested = all_feature_levels.data();
            feature_data.NumFeatureLevels = unsigned(all_feature_levels.size());

            if (SUCCEEDED(test_device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feature_data, sizeof(feature_data))))
            {
                out_max_features = feature_data.MaxSupportedFeatureLevel;
            }
            else
            {
                out_max_features = min_features;
            }

            res_nodes = int(test_device->GetNodeCount());
            CC_ASSERT(res_nodes >= 0);
        }
    });

    return res_nodes;
}

cc::vector<phi::gpu_info> phi::d3d12::get_adapter_candidates()
{
    auto constexpr min_candidate_feature_level = D3D_FEATURE_LEVEL_12_0;

    // Create a temporary factory to enumerate adapters
    shared_com_ptr<IDXGIFactory4> temp_factory;
    detail::perform_safe_seh_call([&] { PHI_D3D12_VERIFY(::CreateDXGIFactory(PHI_COM_WRITE(temp_factory))); });

    // If the call failed (likely XP or earlier), return empty
    if (!temp_factory.is_valid())
        return {};

    cc::vector<gpu_info> res;

    shared_com_ptr<IDXGIAdapter> temp_adapter;
    for (uint32_t i = 0u; temp_factory->EnumAdapters(i, temp_adapter.override()) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        if (temp_adapter.is_valid())
        {
            D3D_FEATURE_LEVEL max_feature_level = D3D_FEATURE_LEVEL(0);
            auto num_nodes = test_adapter(temp_adapter, min_candidate_feature_level, max_feature_level);

            if (num_nodes >= 0)
            {
                // Min level supported, this adapter is a candidate
                DXGI_ADAPTER_DESC adapter_desc;
                PHI_D3D12_VERIFY(temp_adapter->GetDesc(&adapter_desc));


                auto& new_candidate = res.emplace_back();
                new_candidate.vendor = get_gpu_vendor_from_id(adapter_desc.VendorId);
                new_candidate.index = i;

                new_candidate.dedicated_video_memory_bytes = adapter_desc.DedicatedVideoMemory;
                new_candidate.dedicated_system_memory_bytes = adapter_desc.DedicatedSystemMemory;
                new_candidate.shared_system_memory_bytes = adapter_desc.SharedSystemMemory;

                char description_nonwide[sizeof(adapter_desc.Description) + 8];
                std::snprintf(description_nonwide, sizeof(description_nonwide), "%ws", adapter_desc.Description);
                new_candidate.description = cc::string(description_nonwide);

                if (max_feature_level < D3D_FEATURE_LEVEL_12_0)
                    new_candidate.capabilities = gpu_capabilities::insufficient;
                else if (max_feature_level == D3D_FEATURE_LEVEL_12_0)
                    new_candidate.capabilities = gpu_capabilities::level_1;
                else if (max_feature_level == D3D_FEATURE_LEVEL_12_1)
                    new_candidate.capabilities = gpu_capabilities::level_2;
                else
                    new_candidate.capabilities = gpu_capabilities::level_3;
            }
        }
    }

    return res;
}

phi::gpu_feature_flags phi::d3d12::get_gpu_features(ID3D12Device* device)
{
    gpu_feature_flags res = {};

    // for D3D12 feature tiers and how they map to GPUs, see:
    // https://en.wikipedia.org/wiki/Feature_levels_in_Direct3D#Support_matrix

    // Capability checks

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS feat_data = {};
        auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &feat_data, sizeof(feat_data)));

        if (success)
        {
            if (feat_data.ConservativeRasterizationTier != D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED)
            {
                res |= gpu_feature::conservative_raster;
            }
            if (feat_data.ROVsSupported)
            {
                res |= gpu_feature::rasterizer_ordered_views;
            }
        }
    }

    // SM 6.0
    {
        D3D12_FEATURE_DATA_SHADER_MODEL feat_data = {D3D_SHADER_MODEL_6_0};
        auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &feat_data, sizeof(feat_data)));

        if (success && feat_data.HighestShaderModel >= D3D_SHADER_MODEL_6_0)
        {
            res |= gpu_feature::hlsl_sm6;
        }
    }

    // SM 6.0 wave intrinsics
    if (res.has(gpu_feature::hlsl_sm6))
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 feat_data = {};
        auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &feat_data, sizeof(feat_data)));

        if (success && feat_data.WaveOps)
        {
            res |= gpu_feature::hlsl_wave_ops;
        }
    }

    // Device5 (this is purely OS-based, Win10 1809+, aka Redstone 5)
    shared_com_ptr<ID3D12Device5> device5;
    auto const has_device5 = SUCCEEDED(device->QueryInterface(PHI_COM_WRITE(device5)));

    // features requiring windows 1809+
    if (has_device5)
    {
        // Raytracing
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 feat_data = {};
            auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &feat_data, sizeof(feat_data)));
            if (success && feat_data.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
            {
                res |= gpu_feature::raytracing;
            }
        }

        // Variable rate shading
        // NOTE: This feature additionally requires GraphicsCommandList5, which is Win10 1903+, but without it this check fails
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS6 feat_data = {};
            auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &feat_data, sizeof(feat_data)));
            if (success)
            {
                if (feat_data.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_1)
                {
                    res |= gpu_feature::shading_rate_t1;
                }

                if (feat_data.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2)
                {
                    res |= gpu_feature::shading_rate_t2;
                }
            }
        }

        // Mesh and Amplification shaders were added in Win10 20H1, also known as Win10 2004 (Released May 2020)
        // NOTE: Explicitly do not use NTDDI_WIN10_20H1 on the right hand as it isn't defined on previous versions
#if WDK_NTDDI_VERSION >= 0x0A000008
        // Mesh/Amplification shaders
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 feat_data = {};
            auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &feat_data, sizeof(feat_data)));

            if (success && feat_data.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1)
            {
                res |= gpu_feature::mesh_shaders;
            }
        }
#endif
    }


    return res;
}
