#pragma once

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/vulkan_fwd.hh>

namespace phi::vk
{
class ShaderViewPool;
class ResourcePool;
class PipelinePool;
class AccelStructPool;

class ShaderTableConstructor
{
public:
    [[nodiscard]] phi::shader_table_strides calculateShaderTableSizes(arg::shader_table_record const& ray_gen_record,
                                                                      cc::span<arg::shader_table_record const> miss_records,
                                                                      cc::span<arg::shader_table_record const> hit_group_records,
                                                                      cc::span<arg::shader_table_record const> callable_records);

    void writeShaderTable(std::byte* dest, handle::pipeline_state pso, unsigned stride_bytes, cc::span<arg::shader_table_record const> records);

public:
    void initialize(VkDevice device, ShaderViewPool* sv_pool, ResourcePool* resource_pool, PipelinePool* pso_pool, AccelStructPool* as_pool);

private:
    unsigned getShaderRecordSize(cc::span<arg::shader_table_record const> records);

private:
    VkDevice device;
    ShaderViewPool* pool_shader_views;
    ResourcePool* pool_resources;
    PipelinePool* pool_pipeline_states;
    AccelStructPool* pool_accel_structs;
};
}
