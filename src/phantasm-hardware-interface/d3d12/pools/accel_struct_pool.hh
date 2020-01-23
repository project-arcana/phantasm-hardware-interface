#pragma once

#include <mutex>

#include <clean-core/span.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/detail/linked_pool.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>

namespace phi::d3d12
{
class ResourcePool;

class AccelStructPool
{
public:
    [[nodiscard]] handle::accel_struct createBottomLevelAS(cc::span<arg::blas_element const> elements, accel_struct_build_flags_t flags);

    [[nodiscard]] handle::accel_struct createTopLevelAS(unsigned num_instances);

    [[nodiscard]] shader_table_sizes calculateShaderTableSize(handle::accel_struct as,
                                                              arg::shader_table_records ray_gen_records,
                                                              arg::shader_table_records miss_records,
                                                              arg::shader_table_records hit_group_records);

    void free(handle::accel_struct as);
    void free(cc::span<handle::accel_struct const> as);

public:
    void initialize(ID3D12Device5* device, ResourcePool* res_pool, unsigned max_num_accel_structs);
    void destroy();


public:
    struct accel_struct_node
    {
        uint64_t raw_as_handle;
        std::byte* buffer_instances_map;
        handle::resource buffer_as;
        handle::resource buffer_scratch;
        handle::resource buffer_instances;
        accel_struct_build_flags_t flags;
        cc::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;

        void reset()
        {
            raw_as_handle = 0;
            buffer_instances_map = nullptr;
            buffer_as = handle::null_resource;
            buffer_scratch = handle::null_resource;
            buffer_instances = handle::null_resource;
            flags = {};
            geometries.clear();
        }
    };

public:
    accel_struct_node& getNode(handle::accel_struct as);

private:
    accel_struct_node& acquireAccelStruct(handle::accel_struct& out_handle);

    void internalFree(accel_struct_node& node);

private:
    ID3D12Device5* mDevice = nullptr;
    ResourcePool* mResourcePool = nullptr;

    phi::detail::linked_pool<accel_struct_node, unsigned> mPool;

    std::mutex mMutex;
};

}
