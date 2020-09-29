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

phi::shader_table_strides phi::d3d12::ShaderTableConstructor::calculateShaderTableSizes(const arg::shader_table_record& ray_gen_record,
                                                                                        cc::span<arg::shader_table_record const> miss_records,
                                                                                        cc::span<arg::shader_table_record const> hit_group_records,
                                                                                        cc::span<arg::shader_table_record const> callable_records)
{
    shader_table_strides res = {};
    res.size_ray_gen = getShaderRecordSize(cc::span{ray_gen_record});

    res.stride_miss = getShaderRecordSize(miss_records);
    res.size_miss = res.stride_miss * unsigned(miss_records.size());

    res.stride_hit_group = getShaderRecordSize(hit_group_records);
    res.size_hit_group = res.stride_hit_group * unsigned(hit_group_records.size());

    res.stride_callable = getShaderRecordSize(callable_records);
    res.size_callable = res.stride_callable * unsigned(callable_records.size());

    // any individual table record must start at a 64B-aligned address
    static_assert(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT == 64, "shader table alignment wrong");

    return res;
}

void phi::d3d12::ShaderTableConstructor::writeShaderTable(std::byte* dest, handle::pipeline_state pso, unsigned stride_bytes, cc::span<arg::shader_table_record const> records)
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
        if (rec.root_arg_size_bytes > 0)
        {
            std::memcpy(data_ptr_inner, rec.root_arg_data, rec.root_arg_size_bytes);
            // root constants must fill a multiple of 8 bytes
            data_ptr_inner += phi::util::align_up(rec.root_arg_size_bytes, 8u);
        }

        for (auto const& arg : rec.shader_arguments)
        {
            // copy the CBV VA
            if (arg.constant_buffer.is_valid())
            {
                CC_ASSERT(pool_resources->isBufferAccessInBounds(arg.constant_buffer, arg.constant_buffer_offset, 1) && "CBV offset OOB");

                D3D12_GPU_VIRTUAL_ADDRESS const cbv_va = pool_resources->getBufferInfo(arg.constant_buffer).gpu_va + arg.constant_buffer_offset;
                std::memcpy(data_ptr_inner, &cbv_va, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
                data_ptr_inner += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
            }

            if (arg.shader_view.is_valid())
            {
                // copy the SRV/UAV GPU descriptor
                if (pool_shader_views->hasSRVsUAVs(arg.shader_view))
                {
                    D3D12_GPU_DESCRIPTOR_HANDLE const srv_uav_start = pool_shader_views->getSRVUAVGPUHandle(arg.shader_view);
                    std::memcpy(data_ptr_inner, &srv_uav_start, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                    data_ptr_inner += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
                }

                // copy the sampler GPU descriptor
                if (pool_shader_views->hasSamplers(arg.shader_view))
                {
                    D3D12_GPU_DESCRIPTOR_HANDLE const sampler_start = pool_shader_views->getSamplerGPUHandle(arg.shader_view);
                    std::memcpy(data_ptr_inner, &sampler_start, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                    data_ptr_inner += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
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


unsigned phi::d3d12::ShaderTableConstructor::getShaderRecordSize(cc::span<arg::shader_table_record const> records)
{
    unsigned max_num_8byte_blocks = 0;

    for (auto const& rec : records)
    {
        unsigned num_8byte_blocks = 0;

        // root constants in the beginning
        // the root constant section must be packed into 8 byte blocks, ceil the given size to a multiple of 8
        // (effectively 'align_up(size, 8) / 8')
        num_8byte_blocks += cc::int_div_ceil(rec.root_arg_size_bytes, 8u);

        for (auto const& arg : rec.shader_arguments)
        {
            // CBV adds a single GPU VA
            if (arg.constant_buffer.is_valid())
                ++num_8byte_blocks;

            if (arg.shader_view.is_valid())
            {
                // Any SRVs / UAVs add a single descriptor heap pointer
                if (pool_shader_views->hasSRVsUAVs(arg.shader_view))
                    ++num_8byte_blocks;

                // Same for any samplers
                if (pool_shader_views->hasSamplers(arg.shader_view))
                    ++num_8byte_blocks;
            }
        }

        max_num_8byte_blocks = cc::max<uint32_t>(max_num_8byte_blocks, num_8byte_blocks);
    }


    // size of the program identifier, plus 8 bytes per maximum over the record's arguments, aligned to shader record alignment alignment

    auto const size_unaligned = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES // sign of the program identifier
                                + 8u * max_num_8byte_blocks; // all records use as much space for arguments as the largest one: 8 byte per pointer / root constant

    // align correctly
    return phi::util::align_up(size_unaligned, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
}
