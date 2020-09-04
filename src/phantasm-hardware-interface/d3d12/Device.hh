#pragma once

#include <phantasm-hardware-interface/config.hh>
#include <phantasm-hardware-interface/detail/gpu_info.hh>

#include "common/d3d12_fwd.hh"

namespace phi::d3d12
{
class Device
{
public:
    void initialize(IDXGIAdapter& adapter, backend_config const& config);
    void destroy();

    gpu_feature_flags getFeatures() const { return mFeatures; }
    bool hasSM6WaveIntrinsics() const { return mFeatures.has(gpu_feature::hlsl_wave_ops); }
    bool hasRaytracing() const { return mFeatures.has(gpu_feature::raytracing); }
    bool hasVariableRateShading() const { return mFeatures.has_any_of({gpu_feature::shading_rate_t1, gpu_feature::shading_rate_t2}); }

    ID3D12Device5* getDevice() const { return mDevice; }

private:
    ID3D12Device5* mDevice = nullptr;
    gpu_feature_flags mFeatures;

    bool mIsShutdownCrashSubsceptible = false;
    bool mIsShutdownCrashWorkaroundActive = false;
};

}
