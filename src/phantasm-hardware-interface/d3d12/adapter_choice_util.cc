#include "adapter_choice_util.hh"

#include <cstdio>

#ifdef PHI_HAS_OPTICK
#include <optick.h>
#endif

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/common/log.hh>

#include "common/safe_seh_call.hh"
#include "common/sdk_version.hh"
#include "common/shared_com_ptr.hh"
#include "common/verify.hh"

namespace
{
bool testDeviceOnAdapter(IDXGIAdapter* pAdapter, ID3D12Device** ppOptOutDevice)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT("Test/Create ID3D12Device");
#endif

    // if ppOptOutDevice is nullptr, this just tests and does not create the device,
    // which is faster (but slower than testing + creating separately)
    auto const hres = ::D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), IID_PPV_ARGS_Helper(ppOptOutDevice));
    bool const isEligible = SUCCEEDED(hres);

    if (!isEligible)
    {
        DXGI_ADAPTER_DESC adapterDesc = {};
        PHI_D3D12_VERIFY(pAdapter->GetDesc(&adapterDesc));

        // just making sure here, not sure if cc format supports %ws
        char buf[512] = "";
        snprintf(buf, sizeof(buf), "GPU \"%ws\" does not support DirectX 12", adapterDesc.Description);
        PHI_LOG_TRACE("{}", buf);
    }

    return isEligible;
}
} // namespace

uint32_t phi::d3d12::getAdapterCandidates(IDXGIFactory6* factory,
                                          cc::span<phi::gpu_info> outCandidateInfos,
                                          cc::span<ID3D12Device*> outCandidateDevices,
                                          cc::span<IDXGIAdapter*> outCandidateAdapters)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT();
#endif

    CC_ASSERT(!outCandidateInfos.empty() && outCandidateInfos.size() == outCandidateDevices.size() && "output spans unexpected");
    CC_ASSERT(outCandidateInfos.size() == outCandidateAdapters.size() && "output spans unexpected");

    uint32_t numWrittenCandidates = 0;

    IDXGIAdapter* tempAdapter = nullptr;
    for (uint32_t i = 0u; factory->EnumAdapters(i, &tempAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        if (tempAdapter == nullptr)
            continue;

        if (numWrittenCandidates == outCandidateInfos.size())
        {
            PHI_LOG_WARN("More than {} GPUs found, aborting search", numWrittenCandidates);
            break;
        }

        ID3D12Device* testDevice = nullptr;
        // actually creating the device here is not necessary, see this:
        // https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice#remarks
        // for a single GPU, it is slower to test and create separately, but for many GPUs it could be much slower, TODO
        bool const isEligible = testDeviceOnAdapter(tempAdapter, &testDevice);

        if (!isEligible)
        {
            // release the testing device and adapter immediately if ineligible
            if (testDevice)
            {
                testDevice->Release();
            }

            tempAdapter->Release();
            continue;
        }

        // this adapter is a candidate
        uint32_t const newIndex = numWrittenCandidates++;

        // store ID3D12Device we created as a test
        outCandidateDevices[newIndex] = testDevice;

        // store IDXGIAdapter that was used to create that device
        outCandidateAdapters[newIndex] = tempAdapter;

        // write GPU info
        outCandidateInfos[newIndex] = getAdapterInfo(tempAdapter, i);
        tempAdapter = nullptr;
    }

    return numWrittenCandidates;
}

phi::gpu_info phi::d3d12::getAdapterInfo(IDXGIAdapter* adapter, uint32_t index)
{
    phi::gpu_info info;

    DXGI_ADAPTER_DESC adapterDesc;
    PHI_D3D12_VERIFY(adapter->GetDesc(&adapterDesc));
    info.vendor = getGPUVendorFromPCIeID(adapterDesc.VendorId);
    info.index = index;

    info.dedicated_video_memory_bytes = adapterDesc.DedicatedVideoMemory;
    info.dedicated_system_memory_bytes = adapterDesc.DedicatedSystemMemory;
    info.shared_system_memory_bytes = adapterDesc.SharedSystemMemory;

    std::snprintf(info.name, sizeof(info.name), "%ws", adapterDesc.Description);

    return info;
}

bool phi::d3d12::getFirstAdapter(IDXGIFactory6* factory, IDXGIAdapter** outAdapter, ID3D12Device** outDevice, uint32_t* outIndex)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT();
#endif

    IDXGIAdapter* tempAdapter = nullptr;
    for (uint32_t i = 0u; factory->EnumAdapters(i, &tempAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        if (tempAdapter == nullptr)
            continue;

        // Check to see if the adapter supports Direct3D 12 and create it
        // It is significantly faster to do this in one step instead of two calls
        ID3D12Device* newDevice = nullptr;
        if (testDeviceOnAdapter(tempAdapter, &newDevice))
        {
            *outAdapter = tempAdapter;
            *outDevice = newDevice;
            *outIndex = i;
            return true;
        }

        tempAdapter->Release();
    }

    return false;
}

phi::d3d12::gpu_feature_info phi::d3d12::getGPUFeaturesFromDevice(ID3D12Device5* device)
{
    gpu_feature_info res;

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
                res.features |= gpu_feature::conservative_raster;
            }
            if (feat_data.ROVsSupported)
            {
                res.features |= gpu_feature::rasterizer_ordered_views;
            }
        }
    }

    // SM 6.0
    {
        D3D12_FEATURE_DATA_SHADER_MODEL feat_data = {
#if PHI_D3D12_HAS_20H1_FEATURES
            D3D_SHADER_MODEL_6_6
#else
            D3D_SHADER_MODEL_6_5
#endif
        };
        auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &feat_data, sizeof(feat_data)));
        // NOTE: CheckFeatureSupport writes the max of the current value and the highest supported SM version to feat_data
        // - it is not purely an out parameter

        if (success)
        {
            // even with a future SM6.7, this will never return a higher value than SM6.6 as per the behavior above
            switch (feat_data.HighestShaderModel)
            {
            case D3D_SHADER_MODEL_6_0:
                res.sm_version = gpu_feature_info::hlsl_sm6_0;
                break;
            case D3D_SHADER_MODEL_6_1:
                res.sm_version = gpu_feature_info::hlsl_sm6_1;
                break;
            case D3D_SHADER_MODEL_6_2:
                res.sm_version = gpu_feature_info::hlsl_sm6_2;
                break;
            case D3D_SHADER_MODEL_6_3:
                res.sm_version = gpu_feature_info::hlsl_sm6_3;
                break;
            case D3D_SHADER_MODEL_6_4:
                res.sm_version = gpu_feature_info::hlsl_sm6_4;
                break;
            case D3D_SHADER_MODEL_6_5:
                res.sm_version = gpu_feature_info::hlsl_sm6_5;
                break;
#if PHI_D3D12_HAS_20H1_FEATURES
            case D3D_SHADER_MODEL_6_6:
                res.sm_version = gpu_feature_info::hlsl_sm6_6;
                break;
#endif

            default:
                PHI_LOG_WARN("unrecognized HLSL shader model version {}", feat_data.HighestShaderModel);
            case D3D_SHADER_MODEL_5_1:
                res.sm_version = gpu_feature_info::hlsl_sm5_1;
                break;
            }
        }
    }

    // SM 6.0 wave intrinsics
    if (res.sm_version >= gpu_feature_info::hlsl_sm6_0)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 feat_data = {};
        auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &feat_data, sizeof(feat_data)));

        if (success && feat_data.WaveOps)
        {
            res.features |= gpu_feature::hlsl_wave_ops;
        }
    }

    // features requiring windows 1809+ (hard requirement)
    {
        // Raytracing
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 feat_data = {};
            auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &feat_data, sizeof(feat_data)));
            if (success)
            {
                if (feat_data.RaytracingTier == D3D12_RAYTRACING_TIER_1_0)
                {
                    res.raytracing = gpu_feature_info::raytracing_t1_0;
                }
#if PHI_D3D12_HAS_20H1_FEATURES
                else if (feat_data.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1)
                {
                    res.raytracing = gpu_feature_info::raytracing_t1_1;
                }
#endif
            }
        }

        // Variable rate shading
        // NOTE: This feature additionally requires GraphicsCommandList5, which is Win10 1903+, but without it this check fails
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS6 feat_data = {};
            auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &feat_data, sizeof(feat_data)));
            if (success)
            {
                if (feat_data.VariableShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_1)
                {
                    res.variable_rate_shading = gpu_feature_info::variable_rate_shading_t1_0;
                }
                else if (feat_data.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2)
                {
                    res.variable_rate_shading = gpu_feature_info::variable_rate_shading_t2_0;
                }
            }
        }

#if PHI_D3D12_HAS_20H1_FEATURES
        // Mesh/Amplification shaders
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 feat_data = {};
            auto const success = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &feat_data, sizeof(feat_data)));

            if (success && feat_data.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1)
            {
                res.features |= gpu_feature::mesh_shaders;
            }
        }
#endif
    }


    return res;
}
