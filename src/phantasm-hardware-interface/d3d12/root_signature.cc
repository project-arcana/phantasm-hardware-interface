#include "root_signature.hh"

#include <phantasm-hardware-interface/limits.hh>

#include <phantasm-hardware-interface/d3d12/common/log.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

ID3D12RootSignature* pr::backend::d3d12::create_root_signature(ID3D12Device& device,
                                                               cc::span<const CD3DX12_ROOT_PARAMETER> root_params,
                                                               cc::span<const CD3DX12_STATIC_SAMPLER_DESC> samplers,
                                                               root_signature_type type)
{
    CD3DX12_ROOT_SIGNATURE_DESC desc = {};
    desc.pParameters = root_params.empty() ? nullptr : root_params.data();
    desc.NumParameters = UINT(root_params.size());
    desc.pStaticSamplers = samplers.empty() ? nullptr : samplers.data();
    desc.NumStaticSamplers = UINT(samplers.size());

    if (type == root_signature_type::graphics)
    {
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        //            | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
        //                 | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    }
    else if (type == root_signature_type::compute)
    {
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
                     | D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
                     | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    }
    else if (type == root_signature_type::raytrace_local)
    {
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    }
    else if (type == root_signature_type::raytrace_global)
    {
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    }
    else
    {
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        CC_ASSERT(false && "invalid root signature type");
    }

    shared_com_ptr<ID3DBlob> serialized_root_sig;
    shared_com_ptr<ID3DBlob> error_blob;
    auto const serialize_hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, serialized_root_sig.override(), error_blob.override());
    if (serialize_hr == E_INVALIDARG)
    {
        log::err()("root signature serialization failed:\n{}", static_cast<char*>(error_blob->GetBufferPointer()));
    }
    PR_D3D12_ASSERT_FULL(serialize_hr, &device);


    ID3D12RootSignature* res;
    PR_D3D12_VERIFY_FULL(device.CreateRootSignature(0, serialized_root_sig->GetBufferPointer(), serialized_root_sig->GetBufferSize(), IID_PPV_ARGS(&res)), &device);
    return res;
}

pr::backend::d3d12::shader_argument_map pr::backend::d3d12::detail::root_signature_params::add_shader_argument_shape(const pr::backend::arg::shader_argument_shape& shape,
                                                                                                                     bool add_fixed_root_constants)
{
    shader_argument_map res_map;
    auto const argument_visibility = D3D12_SHADER_VISIBILITY_ALL; // NOTE: Eventually arguments could be constrained to stages

    // create root descriptor to CBV
    if (shape.has_cb)
    {
        CD3DX12_ROOT_PARAMETER& root_cbv = root_params.emplace_back();
        root_cbv.InitAsConstantBufferView(0, _space, argument_visibility);
        res_map.cbv_param = uint32_t(root_params.size() - 1);
    }
    else
    {
        res_map.cbv_param = uint32_t(-1);
    }

    // create descriptor table for SRVs and UAVs
    if (shape.num_srvs + shape.num_uavs > 0)
    {
        auto const desc_range_start = _desc_ranges.size();

        if (shape.num_srvs > 0)
        {
            _desc_ranges.emplace_back();
            _desc_ranges.back().Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, shape.num_srvs, 0, _space);
        }

        if (shape.num_uavs > 0)
        {
            _desc_ranges.emplace_back();
            _desc_ranges.back().Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, shape.num_uavs, 0, _space);
        }

        auto const desc_range_end = _desc_ranges.size();

        CD3DX12_ROOT_PARAMETER& desc_table = root_params.emplace_back();
        desc_table.InitAsDescriptorTable(UINT(desc_range_end - desc_range_start), _desc_ranges.data() + desc_range_start, argument_visibility);
        res_map.srv_uav_table_param = uint32_t(root_params.size() - 1);
    }
    else
    {
        res_map.srv_uav_table_param = uint32_t(-1);
    }

    if (shape.num_samplers > 0)
    {
        auto const desc_range_start = _desc_ranges.size();
        _desc_ranges.emplace_back();
        _desc_ranges.back().Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, shape.num_samplers, 0, _space);

        auto const desc_range_end = _desc_ranges.size();

        CD3DX12_ROOT_PARAMETER& desc_table = root_params.emplace_back();
        desc_table.InitAsDescriptorTable(UINT(desc_range_end - desc_range_start), _desc_ranges.data() + desc_range_start, argument_visibility);
        res_map.sampler_table_param = uint32_t(root_params.size() - 1);
    }
    else
    {
        res_map.sampler_table_param = uint32_t(-1);
    }

    if (add_fixed_root_constants)
    {
        CD3DX12_ROOT_PARAMETER& root_consts = root_params.emplace_back();

        static_assert(limits::max_root_constant_bytes % sizeof(DWORD32) == 0, "root constant size not divisible by dword32 size");
        root_consts.InitAsConstants(limits::max_root_constant_bytes / sizeof(DWORD32), 1, _space, argument_visibility);

        res_map.root_const_param = uint32_t(root_params.size() - 1);
    }
    else
    {
        res_map.root_const_param = uint32_t(-1);
    }

    ++_space;
    return res_map;
}

void pr::backend::d3d12::detail::root_signature_params::add_static_sampler(const sampler_config& config)
{
    CD3DX12_STATIC_SAMPLER_DESC& sampler = samplers.emplace_back();
    sampler.Init(static_cast<UINT>(samplers.size() - 1),                                                //
                 util::to_native(config.filter, config.compare_func != sampler_compare_func::disabled), //
                 util::to_native(config.address_u),                                                     //
                 util::to_native(config.address_v),                                                     //
                 util::to_native(config.address_w),                                                     //
                 config.lod_bias,                                                                       //
                 config.max_anisotropy,                                                                 //
                 util::to_native(config.compare_func),                                                  //
                 util::to_native(config.border_color),                                                  //
                 config.min_lod,                                                                        //
                 config.max_lod,                                                                        //
                 D3D12_SHADER_VISIBILITY_ALL,                                                           //
                 0                                                                                      // space 0
    );
}

void pr::backend::d3d12::initialize_root_signature(pr::backend::d3d12::root_signature& root_sig,
                                                   ID3D12Device& device,
                                                   pr::backend::arg::shader_argument_shapes payload_shape,
                                                   bool add_fixed_root_constants,
                                                   root_signature_type type)
{
    detail::root_signature_params parameters;

    for (auto i = 0u; i < payload_shape.size(); ++i)
    {
        auto const& arg_shape = payload_shape[i];
        auto const add_rconsts = add_fixed_root_constants && i == 0;
        root_sig.argument_maps.push_back(parameters.add_shader_argument_shape(arg_shape, add_rconsts));
    }

    root_sig.raw_root_sig = create_root_signature(device, parameters.root_params, parameters.samplers, type);
}
