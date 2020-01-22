#pragma once

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>

namespace pr::backend::d3d12
{
class ShaderViewPool;
class ResourcePool;
class PipelineStateObjectPool;
class AccelStructPool;

class ShaderTableConstructor
{
public:
    [[nodiscard]] pr::backend::shader_table_sizes calculateShaderTableSizes(arg::shader_table_records ray_gen_records,
                                                                            arg::shader_table_records miss_records,
                                                                            arg::shader_table_records hit_group_records);

    void writeShaderTable(std::byte* dest, handle::pipeline_state pso, unsigned stride_bytes, arg::shader_table_records records);

public:
    void initialize(ID3D12Device5* device, ShaderViewPool* sv_pool, ResourcePool* resource_pool, PipelineStateObjectPool* pso_pool, AccelStructPool* as_pool);

private:
    unsigned getShaderRecordSize(pr::backend::arg::shader_table_records records);

private:
    ID3D12Device5* device;
    ShaderViewPool* pool_shader_views;
    ResourcePool* pool_resources;
    PipelineStateObjectPool* pool_pipeline_states;
    AccelStructPool* pool_accel_structs;
};
}
