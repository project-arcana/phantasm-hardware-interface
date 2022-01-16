#pragma once

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/common/container/unique_buffer.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/common/d3dx12.hh>
#include <phantasm-hardware-interface/d3d12/common/shared_com_ptr.hh>

namespace phi::d3d12
{
struct shader_argument_map
{
    uint32_t cbv_param;
    uint32_t srv_uav_table_param;
    uint32_t sampler_table_param;
    uint32_t root_const_param;
};

enum class root_signature_type : uint8_t
{
    graphics,
    compute,
    raytrace_local,
    raytrace_global
};

namespace detail
{
// allows constructive creation of a root signature by combining shader argument shapes
struct root_signature_params
{
    cc::capped_vector<CD3DX12_ROOT_PARAMETER, 16> root_params;
    cc::capped_vector<CD3DX12_STATIC_SAMPLER_DESC, 16> samplers;

    // add_fixed_root_constants: additionally create a fixed root constant field in b1, current space
    // size: limits::max_root_constant_bytes
    [[nodiscard]] shader_argument_map add_shader_argument_shape(arg::shader_arg_shape const& shape,
                                                                bool add_fixed_root_constants,
                                                                uint32_t num_overlapped_srv_ranges,
                                                                uint32_t num_overlapped_uav_ranges,
                                                                uint32_t num_overlapped_sampler_ranges);
    void add_static_sampler(sampler_config const& config);

private:
    unsigned _space = 0;
    cc::capped_vector<CD3DX12_DESCRIPTOR_RANGE, 16> _desc_ranges;
};
} // namespace detail

// creates a root signature from parameters and samplers
[[nodiscard]] ID3D12RootSignature* create_root_signature(ID3D12Device& device,
                                                         cc::span<CD3DX12_ROOT_PARAMETER const> root_params,
                                                         cc::span<CD3DX12_STATIC_SAMPLER_DESC const> samplers,
                                                         root_signature_type type);

struct root_signature
{
    ID3D12RootSignature* raw_root_sig = nullptr;
    cc::capped_vector<shader_argument_map, limits::max_shader_arguments> argument_maps;
};

// add_fixed_root_constants: create a fixed root constant field in register(b1, space0)
void initialize_root_signature(root_signature* pOutRootSignature, ID3D12Device* pDevice, arg::root_signature_description const& desc, root_signature_type typ);

ID3D12CommandSignature* createCommandSignatureForDraw(ID3D12Device* pDevice);

ID3D12CommandSignature* createCommandSignatureForDrawIndexed(ID3D12Device* pDevice);

ID3D12CommandSignature* createCommandSignatureForDispatch(ID3D12Device* pDevice);

ID3D12CommandSignature* createCommandSignatureForDrawIndexedWithID(ID3D12Device* pDevice, ID3D12RootSignature* pRootSig);
} // namespace phi::d3d12
