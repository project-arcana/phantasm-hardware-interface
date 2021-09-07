#pragma once

#include <cstdint>

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/types.hh>

#include "common/d3d12_sanitized.hh"
#include "common/gpu_feature_info.hh"

namespace phi::d3d12
{
[[nodiscard]] gpu_feature_info getGPUFeaturesFromDevice(ID3D12Device5* device);

// Get all available adapter candidates
uint32_t getAdapterCandidates(IDXGIFactory4* factory,
                              cc::span<phi::gpu_info> outCandidateInfos,
                              cc::span<ID3D12Device*> outCandidateDevices,
                              cc::span<IDXGIAdapter*> outCandidateAdapters);

phi::gpu_info getAdapterInfo(IDXGIAdapter* adapter, uint32_t index);

// get the first supported adapter, faster than getAdapterCandidates
bool getFirstAdapter(IDXGIFactory4* factory, IDXGIAdapter** outAdapter, ID3D12Device** outDevice, uint32_t* outIndex);
} // namespace phi::d3d12
