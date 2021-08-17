#include "shader_table_construction.hh"

#include <cstdio>

#include <clean-core/native/wchar_conversion.hh>
#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/common/log_util.hh>

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
    CC_ASSERT(PHI_IMPLICATION(stride_bytes == 0, records.size() == 1) && "if no stride is specified, no more than a single record is allowed");

    auto const& pso_info = pool_pipeline_states->getRaytrace(pso);

    std::byte* data_ptr_outer = dest;
    for (auto const& rec : records)
    {
        std::byte* data_ptr_inner = data_ptr_outer;

        PipelineStateObjectPool::pso_argument_info arg_info_verification;

        // write the shader identifier
        if (rec.target_type == arg::shader_table_record::e_target_identifiable_shader)
        {
            CC_ASSERT(rec.target_index < pso_info.identifiable_shader_infos.size() && "shader table record - identifiable shader index OOB");

            auto const& identifiable_info = pso_info.identifiable_shader_infos[rec.target_index];
            std::memcpy(data_ptr_inner, identifiable_info.shader_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

            arg_info_verification = identifiable_info.arg_info;
        }
        else // (e_target_hitgroup)
        {
            CC_ASSERT(rec.target_type == arg::shader_table_record::e_target_hitgroup);
            CC_ASSERT(rec.target_index < pso_info.hitgroup_infos.size() && "shader table record - hitgroup index OOB");

            auto const& hitgroup_info = pso_info.hitgroup_infos[rec.target_index];
            std::memcpy(data_ptr_inner, hitgroup_info.shader_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

            arg_info_verification = hitgroup_info.arg_info;
        }

        data_ptr_inner += D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

        for (auto i = 0u; i < rec.shader_arguments.size(); ++i)
        {
            auto const& arg = rec.shader_arguments[i];

            // write the CBV VA
            if (arg.constant_buffer.is_valid())
            {
                CC_ASSERT(pool_resources->isBufferAccessInBounds(arg.constant_buffer, arg.constant_buffer_offset, 1) && "CBV offset would cause an OOB access on GPU");
                CC_ASSERT(arg_info_verification.has_cbv(i) && "shader table write invalid - writing CBV where none is required");

                D3D12_GPU_VIRTUAL_ADDRESS const cbv_va = pool_resources->getBufferInfo(arg.constant_buffer).gpu_va + arg.constant_buffer_offset;
                std::memcpy(data_ptr_inner, &cbv_va, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
                data_ptr_inner += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
            }
            else
            {
                CC_ASSERT(!arg_info_verification.has_cbv(i) && "shader table write invalid - omitting CBV where its required");
            }

            if (arg.shader_view.is_valid())
            {
                // write the SRV/UAV GPU descriptor
                if (pool_shader_views->hasSRVsUAVs(arg.shader_view))
                {
                    CC_ASSERT(arg_info_verification.has_srv_uav(i) && "shader table write invalid - writing shader_view with SRVs/UAVs where none are required");

                    D3D12_GPU_DESCRIPTOR_HANDLE const srv_uav_start = pool_shader_views->getSRVUAVGPUHandle(arg.shader_view);
                    std::memcpy(data_ptr_inner, &srv_uav_start, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                    data_ptr_inner += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
                }
                else
                {
                    CC_ASSERT(!arg_info_verification.has_srv_uav(i) && "shader table write invalid - writing shader_view without SRVs/UAVs where they are required");
                }

                // write the sampler GPU descriptor
                if (pool_shader_views->hasSamplers(arg.shader_view))
                {
                    CC_ASSERT(arg_info_verification.has_sampler(i) && "shader table write invalid - writing shader_view with samplers where none are required");

                    D3D12_GPU_DESCRIPTOR_HANDLE const sampler_start = pool_shader_views->getSamplerGPUHandle(arg.shader_view);
                    std::memcpy(data_ptr_inner, &sampler_start, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                    data_ptr_inner += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
                }
                else
                {
                    CC_ASSERT(!arg_info_verification.has_sampler(i) && "shader table write invalid - writing shader_view without samplers where they are required");
                }
            }
            else
            {
                CC_ASSERT(!arg_info_verification.has_srv_uav(i) && !arg_info_verification.has_sampler(i)
                          && "shader table write invalid - omitting shader_view where its required");
            }
        }

        // write the root constants last
        if (rec.root_arg_size_bytes > 0)
        {
            CC_ASSERT(arg_info_verification.has_root_consts() && "shader table write invalid - writing root constants where none are required");

            std::memcpy(data_ptr_inner, rec.root_arg_data, rec.root_arg_size_bytes);
            // root constants must fill a multiple of 8 bytes
            data_ptr_inner += phi::util::align_up(rec.root_arg_size_bytes, 8u);
        }
        else
        {
            CC_ASSERT(!arg_info_verification.has_root_consts() && "shader table write invalid - omitting root constants where they are required");
        }

        data_ptr_outer += stride_bytes;

        // if these are multiple records (and thus stride is > 0), ptr_outer must be advanced at least enough to not
        // override the current entries in the next iteration
        CC_ASSERT(PHI_IMPLICATION(stride_bytes > 0, data_ptr_inner <= data_ptr_outer) && "stride too small for shader table record");
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

        // root constants at the end
        // the root constant section must be packed into 8 byte blocks, ceil the given size to a multiple of 8
        // (effectively 'align_up(size, 8) / 8')
        num_8byte_blocks += rec.root_arg_size_bytes ? cc::int_div_ceil(rec.root_arg_size_bytes, 8u) : 0;

        max_num_8byte_blocks = cc::max<uint32_t>(max_num_8byte_blocks, num_8byte_blocks);
    }


    // size of the program identifier, plus 8 bytes per maximum over the record's arguments, aligned to shader record alignment alignment

    auto const size_unaligned = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES // sign of the program identifier
                                + 8u * max_num_8byte_blocks; // all records use as much space for arguments as the largest one: 8 byte per pointer / root constant

    // align correctly
    return phi::util::align_up(size_unaligned, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
}
