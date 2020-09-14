#include "shader_table_construction.hh"

#include <clean-core/native/wchar_conversion.hh>
#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/log.hh>

#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>
#include <phantasm-hardware-interface/d3d12/pipeline_state.hh>

#include "pools/accel_struct_pool.hh"
#include "pools/pso_pool.hh"
#include "pools/resource_pool.hh"
#include "pools/shader_view_pool.hh"

phi::shader_table_sizes phi::d3d12::ShaderTableConstructor::calculateShaderTableSizes(const arg::shader_table_record& ray_gen_record,
                                                                                      phi::arg::shader_table_records miss_records,
                                                                                      phi::arg::shader_table_records hit_group_records,
                                                                                      arg::shader_table_records callable_records)
{
    shader_table_sizes res = {};
    res.size_ray_gen = getShaderRecordSize(cc::span{ray_gen_record});

    res.stride_miss = getShaderRecordSize(miss_records);
    res.size_miss = res.stride_miss * unsigned(miss_records.size());

    res.stride_hit_group = getShaderRecordSize(hit_group_records);
    res.size_hit_group = res.stride_hit_group * unsigned(hit_group_records.size());

    res.stride_callable = getShaderRecordSize(callable_records);
    res.size_callable = res.stride_callable * unsigned(callable_records.size());

    // any individual table must start at a 64B-aligned address
    static_assert(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT == 64, "shader table alignment wrong");

    res.offset_ray_gen = 0;
    res.offset_miss = phi::util::align_up(res.offset_ray_gen + res.size_ray_gen, 64);
    res.offset_hit_group = phi::util::align_up(res.offset_miss + res.size_miss, 64);
    res.offset_callable = phi::util::align_up(res.offset_hit_group + res.size_hit_group, 64);

    res.total_size = res.offset_callable + res.size_callable;

    return res;
}

void phi::d3d12::ShaderTableConstructor::writeShaderTable(std::byte* dest, handle::pipeline_state pso, unsigned stride_bytes, arg::shader_table_records records)
{
    CC_ASSERT(pool_pipeline_states->isRaytracingPipeline(pso) && "invalid or non-raytracing PSO given");
    auto const& pso_info = pool_pipeline_states->getRaytrace(pso);

    CC_ASSERT(stride_bytes == 0 ? records.size() == 1 : true && "if no stride is specified, no more than a single record is allowed");

    std::byte* data_ptr_outer = dest;
    for (auto const& rec : records)
    {
        std::byte* data_ptr_inner = data_ptr_outer;

        // copy the shader identifier
        if (rec.target_type == arg::shader_table_record::e_target_identifiable_shader)
        {
            CC_ASSERT(rec.target_index < pso_info.export_infos.size() && "shader table record - shader index OOB");
            std::memcpy(data_ptr_inner, pso_info.export_infos[rec.target_index].shader_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
        else // (e_target_hitgroup)
        {
            CC_ASSERT(rec.target_index < pso_info.hitgroup_infos.size() && "shader table record - hitgroup index OOB");
            std::memcpy(data_ptr_inner, pso_info.hitgroup_infos[rec.target_index].shader_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }

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
                D3D12_GPU_VIRTUAL_ADDRESS const cbv_va = pool_resources->getRawResource(arg.constant_buffer)->GetGPUVirtualAddress() + arg.constant_buffer_offset;
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
