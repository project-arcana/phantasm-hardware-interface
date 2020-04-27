#pragma once

#include <phantasm-hardware-interface/config.hh>
#include <phantasm-hardware-interface/gpu_info.hh>

#include "common/d3d12_fwd.hh"
#include "common/shared_com_ptr.hh"

namespace phi::d3d12
{
class Device
{
public:
    Device() = default;
    Device(Device const&) = delete;
    Device(Device&&) noexcept = delete;
    Device& operator=(Device const&) = delete;
    Device& operator=(Device&&) noexcept = delete;

    void initialize(IDXGIAdapter& adapter, backend_config const& config);

    gpu_feature_flags getFeatures() const { return mFeatures; }
    bool hasSM6WaveIntrinsics() const { return mFeatures.has(gpu_feature::hlsl_wave_ops); }
    bool hasRaytracing() const { return mFeatures.has(gpu_feature::raytracing); }
    bool hasVariableRateShading() const { return mFeatures.has_any_of({gpu_feature::shading_rate_t1, gpu_feature::shading_rate_t2}); }

    ID3D12Device& getDevice() const { return *mDevice.get(); }
    shared_com_ptr<ID3D12Device> const& getDeviceShared() const { return mDevice; }

    ID3D12Device1& getDevice1() const { return *mDevice1.get(); }
    ID3D12Device5* getDevice5() const { return mDevice5.get(); }

private:
    shared_com_ptr<ID3D12DeviceRemovedExtendedDataSettings> mDREDSettings;
    shared_com_ptr<ID3D12Device> mDevice;
    shared_com_ptr<ID3D12Device1> mDevice1;
    shared_com_ptr<ID3D12Device5> mDevice5;
    gpu_feature_flags mFeatures;
};

}
