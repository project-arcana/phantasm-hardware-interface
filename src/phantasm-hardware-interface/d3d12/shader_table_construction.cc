#include "shader_table_construction.hh"

#include <clean-core/native/wchar_conversion.hh>
#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/detail/byte_util.hh>
#include <phantasm-hardware-interface/detail/log.hh>

#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>
#include <phantasm-hardware-interface/d3d12/pipeline_state.hh>

#include "pools/accel_struct_pool.hh"
#include "pools/pso_pool.hh"
#include "pools/resource_pool.hh"
#include "pools/shader_view_pool.hh"

phi::shader_table_sizes phi::d3d12::ShaderTableConstructor::calculateShaderTableSizes(phi::arg::shader_table_records ray_gen_records,
                                                                                      phi::arg::shader_table_records miss_records,
                                                                                      phi::arg::shader_table_records hit_group_records)
{
    shader_table_sizes res = {};
    res.ray_gen_stride_bytes = getShaderRecordSize(ray_gen_records);
    res.miss_stride_bytes = getShaderRecordSize(miss_records);
    res.hit_group_stride_bytes = getShaderRecordSize(hit_group_records);
    return res;
}

void phi::d3d12::ShaderTableConstructor::writeShaderTable(std::byte* dest, phi::handle::pipeline_state pso, unsigned stride_bytes, phi::arg::shader_table_records records)
{
    CC_ASSERT(pool_pipeline_states->isRaytracingPipeline(pso) && "invalid or non-raytracing PSO given");
    auto const pso_state_props = pool_pipeline_states->getRaytrace(pso).raw_state_object_props;

    std::byte* data_ptr_outer = dest;
    for (auto const& rec : records)
    {
        std::byte* data_ptr_inner = data_ptr_outer;

        wchar_t shader_symbol_wide[512];
        int const num_wchars = cc::char_to_widechar(shader_symbol_wide, rec.symbol);
        CC_ASSERT(num_wchars < int(sizeof(shader_symbol_wide) / sizeof(wchar_t)) - 1 && "shader symbol string too large");

        // copy the shader identifier
        void* symbol_id_data = pso_state_props->GetShaderIdentifier(shader_symbol_wide);
        CC_ASSERT(symbol_id_data != nullptr && "unknown shader symbol in shader_table_record");

        std::memcpy(data_ptr_inner, symbol_id_data, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        data_ptr_inner += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

        // copy the root constants
        if (rec.root_arg_size > 0)
        {
            std::memcpy(data_ptr_inner, rec.root_arg_data, rec.root_arg_size);
            data_ptr_inner += rec.root_arg_size;
        }

        for (auto const& arg : rec.shader_arguments)
        {
            // copy the CBV VA
            if (arg.constant_buffer.is_valid())
            {
                D3D12_GPU_VIRTUAL_ADDRESS const cbv_va = pool_resources->getRawResource(arg.constant_buffer)->GetGPUVirtualAddress();
                std::memcpy(data_ptr_inner, &cbv_va, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
                data_ptr_inner += sizeof(void*);
            }

            if (arg.shader_view.is_valid())
            {
                // copy the SRV/UAV GPU descriptor
                if (pool_shader_views->hasSRVsUAVs(arg.shader_view))
                {
                    D3D12_GPU_DESCRIPTOR_HANDLE const srv_uav_start = pool_shader_views->getSRVUAVGPUHandle(arg.shader_view);
                    std::memcpy(data_ptr_inner, &srv_uav_start, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                    data_ptr_inner += sizeof(void*);
                }

                // copy the sampler GPU descriptor
                if (pool_shader_views->hasSamplers(arg.shader_view))
                {
                    D3D12_GPU_DESCRIPTOR_HANDLE const sampler_start = pool_shader_views->getSamplerGPUHandle(arg.shader_view);
                    std::memcpy(data_ptr_inner, &sampler_start, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                    data_ptr_inner += sizeof(void*);
                }
            }
        }

        data_ptr_outer += stride_bytes;
    }
}

void phi::d3d12::ShaderTableConstructor::initialize(ID3D12Device5* device,
                                                    phi::d3d12::ShaderViewPool* sv_pool,
                                                    phi::d3d12::ResourcePool* resource_pool,
                                                    phi::d3d12::PipelineStateObjectPool* pso_pool,
                                                    phi::d3d12::AccelStructPool* as_pool)
{
    this->device = device;
    this->pool_shader_views = sv_pool;
    this->pool_resources = resource_pool;
    this->pool_pipeline_states = pso_pool;
    this->pool_accel_structs = as_pool;
}


unsigned phi::d3d12::ShaderTableConstructor::getShaderRecordSize(phi::arg::shader_table_records records)
{
    unsigned max_num_args = 0;
    for (auto const& rec : records)
    {
        CC_ASSERT(rec.root_arg_size % sizeof(DWORD32) == 0 && "non-round dword amount");

        // root constants in the beginning
        unsigned num_args = (rec.root_arg_size / uint32_t(sizeof(DWORD32)));

        for (auto const& arg : rec.shader_arguments)
        {
            // CBV adds a single GPU VA
            if (arg.constant_buffer.is_valid())
                ++num_args;

            if (arg.shader_view.is_valid())
            {
                // Any SRVs / UAVs add a single descriptor heap pointer
                if (pool_shader_views->hasSRVsUAVs(arg.shader_view))
                    ++num_args;

                // Same for any samplers
                if (pool_shader_views->hasSamplers(arg.shader_view))
                    ++num_args;
            }
        }

        max_num_args = cc::max<uint32_t>(max_num_args, num_args);
    }

    // size of the program identifier, plus 8 bytes per maximum over the record's arguments, aligned to shader record alignment alignment
    return phi::util::align_up(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 8u * max_num_args, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
}
