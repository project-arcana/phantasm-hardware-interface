#pragma once

#include <mutex>

#include <clean-core/alloc_vector.hh>
#include <clean-core/span.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/common/container/linked_pool.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/fwd.hh>

namespace phi::d3d12
{
class AccelStructPool
{
public:
    [[nodiscard]] handle::accel_struct createBottomLevelAS(cc::span<arg::blas_element const> elements, accel_struct_build_flags_t flags);

    [[nodiscard]] handle::accel_struct createTopLevelAS(unsigned num_instances, accel_struct_build_flags_t flags);

    [[nodiscard]] shader_table_strides calculateShaderTableSize(handle::accel_struct as,
                                                                cc::span<arg::shader_table_record const> ray_gen_records,
                                                                cc::span<arg::shader_table_record const> miss_records,
                                                                cc::span<arg::shader_table_record const> hit_group_records);

    void free(handle::accel_struct as);
    void free(cc::span<handle::accel_struct const> as);

public:
    void initialize(ID3D12Device5* device, ResourcePool* res_pool, unsigned max_num_accel_structs, cc::allocator* static_alloc, cc::allocator* dynamic_alloc);
    void destroy();


public:
    struct accel_struct_node
    {
        // d3d12 GPU VA, "raw native handle" in phi naming
        D3D12_GPU_VIRTUAL_ADDRESS buffer_as_va;
        handle::resource buffer_as;
        handle::resource buffer_scratch;
        accel_struct_build_flags_t flags;
        cc::alloc_vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;

        void reset(cc::allocator* dyn_alloc, unsigned num_geom_reserve);
    };

public:
    accel_struct_node& getNode(handle::accel_struct as);

private:
    accel_struct_node& acquireAccelStruct(handle::accel_struct& out_handle);

    void internalFree(accel_struct_node& node);

private:
    ID3D12Device5* mDevice = nullptr;
    ResourcePool* mResourcePool = nullptr;
    cc::allocator* mDynamicAllocator = nullptr;

    phi::linked_pool<accel_struct_node> mPool;

    std::mutex mMutex;
};

}
