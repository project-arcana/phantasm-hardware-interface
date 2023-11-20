#pragma once

#include <phantasm-hardware-interface/config.hh>

#include "common/d3d12_fwd.hh"
#include "common/gpu_feature_info.hh"

namespace phi::d3d12
{
class Device
{
public:
    bool initialize(ID3D12Device* deviceToUse, IDXGIAdapter& adapter, backend_config const& config);
    void destroy();

    bool hasSM6WaveIntrinsics() const { return mFeatures.features.has(gpu_feature::hlsl_wave_ops); }
    bool hasRaytracing() const { return mIsRaytracingEnabled; }
    bool hasVariableRateShading() const { return mFeatures.variable_rate_shading >= gpu_feature_info::variable_rate_shading_t1_0; }

    ID3D12Device5* getDevice() const { return mDevice; }

private:
    ID3D12Device5* mDevice = nullptr;
    gpu_feature_info mFeatures;
    // whether raytracing is enabled in the config AND available
    // there is nothing to "not init" on the D3D12 side with disabled (but available) RT, this is for phi-internals only
    bool mIsRaytracingEnabled = false;

    bool mIsShutdownCrashSubsceptible = false;
    bool mIsShutdownCrashWorkaroundActive = false;
};

}
