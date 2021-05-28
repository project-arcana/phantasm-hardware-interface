#pragma once

#include <phantasm-hardware-interface/features/gpu_info.hh>
#include <phantasm-hardware-interface/fwd.hh>

#include "common/d3d12_fwd.hh"

namespace phi::d3d12
{
/// Represents a IDXGIAdapter, the uppermost object in the D3D12 hierarchy
class Adapter
{
public:
    // outCreatedDevice: The device created on the chosen physical GPU
    // it has to be created in the GPU choice process and is expensive to re-create
    void initialize(backend_config const& config, ID3D12Device*& outCreatedDevice);

    void destroy();

    bool isValid() const { return mAdapter != nullptr; }

    IDXGIAdapter3& getAdapter() const { return *mAdapter; }
    IDXGIFactory4& getFactory() const { return *mFactory; }

    gpu_info const& getGPUInfo() const { return mGPUInfo; }

private:
    gpu_info mGPUInfo;
    IDXGIAdapter3* mAdapter = nullptr;
    IDXGIFactory4* mFactory = nullptr;
    IDXGIInfoQueue* mInfoQueue = nullptr;
};

} // namespace phi::d3d12
