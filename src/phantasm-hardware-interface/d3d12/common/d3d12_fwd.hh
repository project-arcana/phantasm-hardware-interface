#pragma once

#include <clean-core/native/win32_fwd.hh>

// Forward declarations of common D3D12 entities
// extend as needed

typedef unsigned __int64 UINT64;
typedef UINT64 D3D12_GPU_VIRTUAL_ADDRESS;
typedef struct ID3D10Blob ID3DBlob;

struct ID3D12Device;
struct ID3D12Device1;
struct ID3D12Device5;
struct ID3D12Fence;
struct ID3D12Resource;
struct ID3D12CommandQueue;
struct ID3D12DescriptorHeap;
struct ID3D12RootSignature;
struct ID3D12PipelineState;
struct ID3D12GraphicsCommandList;
struct ID3D12GraphicsCommandList5;
struct ID3D12DeviceRemovedExtendedDataSettings;
struct ID3D12StateObjectProperties;

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11On12Device2;

struct IDXGIAdapter;
struct IDXGIAdapter3;
struct IDXGIFactory4;
struct IDXGISwapChain3;
struct IDXGIInfoQueue;

struct IDXGraphicsAnalysis;

struct D3D12_SHADER_BYTECODE;
struct D3D12_INPUT_ELEMENT_DESC;

struct D3D12_RAYTRACING_GEOMETRY_DESC;

enum D3D12_RESOURCE_STATES;
