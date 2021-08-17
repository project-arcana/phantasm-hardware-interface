#include "shader_table_construction.hh"

#include <cstdio>

#include <clean-core/native/wchar_conversion.hh>
#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/common/log_util.hh>

#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/util.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>

#include "pools/accel_struct_pool.hh"
#include "pools/pipeline_pool.hh"
#include "pools/resource_pool.hh"
#include "pools/shader_view_pool.hh"

phi::shader_table_strides phi::vk::ShaderTableConstructor::calculateShaderTableSizes(const arg::shader_table_record& ray_gen_record,
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

    return res;
}

void phi::vk::ShaderTableConstructor::writeShaderTable(std::byte* dest, handle::pipeline_state pso, unsigned stride_bytes, cc::span<arg::shader_table_record const> records)
{
    CC_ASSERT(false && "unimplemented");
}

void phi::vk::ShaderTableConstructor::initialize(
    VkDevice device, phi::vk::ShaderViewPool* sv_pool, phi::vk::ResourcePool* resource_pool, phi::vk::PipelinePool* pso_pool, phi::vk::AccelStructPool* as_pool)
{
    this->device = device;
    this->pool_shader_views = sv_pool;
    this->pool_resources = resource_pool;
    this->pool_pipeline_states = pso_pool;
    this->pool_accel_structs = as_pool;
}


unsigned phi::vk::ShaderTableConstructor::getShaderRecordSize(cc::span<arg::shader_table_record const> records)
{
    CC_ASSERT(false && "unimplemented");
    return 0;
}
