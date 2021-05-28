#pragma once

#include <cstdint>

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/types.hh>

#include "common/d3d12_sanitized.hh"
#include "common/gpu_feature_info.hh"

namespace phi::d3d12
{
[[nodiscard]] gpu_feature_info getGPUFeaturesFromDevice(ID3D12Device5* device);

// Test the given adapter by creating a device with the min_feature level, returns true if the GPU is eligible
bool testAdapterForFeatures(IDXGIAdapter* adapter, D3D_FEATURE_LEVEL& outMaxFeatures, ID3D12Device*& outDevice);

// Get all available adapter candidates
uint32_t getAdapterCandidates(IDXGIFactory4* factory,
                              cc::span<phi::gpu_info> outCandidateInfos,
                              cc::span<ID3D12Device*> outCandidateDevices,
                              cc::span<IDXGIAdapter*> outCandidateAdapters);
} // namespace phi::d3d12
