#include "accel_struct_pool.hh"

#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/d3d12/common/log.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

#include "resource_pool.hh"

phi::handle::accel_struct phi::d3d12::AccelStructPool::createBottomLevelAS(cc::span<const phi::arg::blas_element> elements,
                                                                                           accel_struct_build_flags_t flags)
{
    handle::accel_struct res_handle;
    accel_struct_node& new_node = acquireAccelStruct(res_handle);
    new_node.reset();

    new_node.geometries.reserve(elements.size());

    // build the D3D12_RAYTRACING_GEOMETRY_DESCs from the vertex/index buffer pairs
    for (auto const& elem : elements)
    {
        auto const& vert_info = mResourcePool->getBufferInfo(elem.vertex_buffer);

        D3D12_RAYTRACING_GEOMETRY_DESC& egeom = new_node.geometries.emplace_back();
        egeom = {};
        egeom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        egeom.Triangles.Transform3x4 = 0;
        egeom.Triangles.VertexBuffer.StartAddress = mResourcePool->getRawResource(elem.vertex_buffer)->GetGPUVirtualAddress();
        egeom.Triangles.VertexBuffer.StrideInBytes = vert_info.stride;
        egeom.Triangles.VertexCount = elem.num_vertices;
        egeom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;


        if (elem.index_buffer.is_valid())
        {
            auto const index_stride = mResourcePool->getBufferInfo(elem.index_buffer).stride;

            egeom.Triangles.IndexBuffer = mResourcePool->getRawResource(elem.index_buffer)->GetGPUVirtualAddress();
            egeom.Triangles.IndexCount = elem.num_indices;
            egeom.Triangles.IndexFormat = index_stride == sizeof(uint16_t) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        }
        else
        {
            egeom.Triangles.IndexBuffer = 0;
            egeom.Triangles.IndexCount = 0;
            egeom.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
        }

        egeom.Flags = elem.is_opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
    }

    // Assemble the bottom level AS object
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC as_create_info = {};
    as_create_info.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    as_create_info.Inputs.Flags = util::to_native_flags(flags);
    as_create_info.Inputs.NumDescs = static_cast<UINT>(new_node.geometries.size());
    as_create_info.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    as_create_info.Inputs.pGeometryDescs = new_node.geometries.data();

    // Query sizes for scratch and result buffers
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info = {};
    mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&as_create_info.Inputs, &prebuild_info);
    CC_ASSERT(prebuild_info.ResultDataMaxSizeInBytes > 0);

    new_node.flags = flags;

    // Create scratch and result buffers
    new_node.buffer_as
        = mResourcePool->createBufferInternal(prebuild_info.ResultDataMaxSizeInBytes, 0, true, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    new_node.buffer_scratch = mResourcePool->createBufferInternal(
        cc::max<UINT64>(prebuild_info.ScratchDataSizeInBytes, prebuild_info.UpdateScratchDataSizeInBytes), 0, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // query GPU address (raw native handle)
    new_node.raw_as_handle = mResourcePool->getRawResource(new_node.buffer_as)->GetGPUVirtualAddress();

    return res_handle;
}

phi::handle::accel_struct phi::d3d12::AccelStructPool::createTopLevelAS(unsigned num_instances)
{
    static_assert(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) == sizeof(accel_struct_geometry_instance), "acceleration instance struct sizes mismatch");

    handle::accel_struct res_handle;
    accel_struct_node& new_node = acquireAccelStruct(res_handle);
    new_node.reset();

    // Assemble the bottom level AS object
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC as_create_info = {};
    as_create_info.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    as_create_info.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE; // TODO
    as_create_info.Inputs.NumDescs = num_instances;
    as_create_info.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    as_create_info.Inputs.pGeometryDescs = nullptr;

    // Query sizes for scratch and result buffers
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info = {};
    mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&as_create_info.Inputs, &prebuild_info);
    CC_ASSERT(prebuild_info.ResultDataMaxSizeInBytes > 0);

    // Create scratch and result buffers
    new_node.buffer_as
        = mResourcePool->createBufferInternal(prebuild_info.ResultDataMaxSizeInBytes, 0, true, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    new_node.buffer_scratch = mResourcePool->createBufferInternal(
        cc::max<UINT64>(prebuild_info.ScratchDataSizeInBytes, prebuild_info.UpdateScratchDataSizeInBytes), 0, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    new_node.buffer_instances = mResourcePool->createMappedBuffer(sizeof(accel_struct_geometry_instance) * num_instances);
    new_node.buffer_instances_map = mResourcePool->getMappedMemory(new_node.buffer_instances);

    // query GPU address (raw native handle)
    new_node.raw_as_handle = mResourcePool->getRawResource(new_node.buffer_as)->GetGPUVirtualAddress();

    return res_handle;
}

void phi::d3d12::AccelStructPool::free(phi::handle::accel_struct as)
{
    if (!as.is_valid())
        return;

    accel_struct_node& freed_node = mPool.get(static_cast<unsigned>(as.index));
    internalFree(freed_node);

    {
        auto lg = std::lock_guard(mMutex);
        mPool.release(static_cast<unsigned>(as.index));
    }
}

void phi::d3d12::AccelStructPool::free(cc::span<const phi::handle::accel_struct> as_span)
{
    auto lg = std::lock_guard(mMutex);

    for (auto as : as_span)
    {
        if (as.is_valid())
        {
            accel_struct_node& freed_node = mPool.get(static_cast<unsigned>(as.index));
            internalFree(freed_node);
            mPool.release(static_cast<unsigned>(as.index));
        }
    }
}

void phi::d3d12::AccelStructPool::initialize(ID3D12Device5* device, phi::d3d12::ResourcePool* res_pool, unsigned max_num_accel_structs)
{
    CC_ASSERT(mDevice == nullptr && mResourcePool == nullptr && "double init");
    mDevice = device;
    mResourcePool = res_pool;
    mPool.initialize(max_num_accel_structs);
}

void phi::d3d12::AccelStructPool::destroy()
{
    if (mDevice != nullptr)
    {
        auto num_leaks = 0;
        mPool.iterate_allocated_nodes([&](accel_struct_node& leaked_node, unsigned) {
            ++num_leaks;
            internalFree(leaked_node);
        });

        if (num_leaks > 0)
        {
            log::info()("warning: leaked {} handle::accel_struct object{}", num_leaks, num_leaks == 1 ? "" : "s");
        }
    }
}

phi::d3d12::AccelStructPool::accel_struct_node& phi::d3d12::AccelStructPool::getNode(phi::handle::accel_struct as)
{
    CC_ASSERT(as.is_valid());
    return mPool.get(static_cast<unsigned>(as.index));
}

phi::d3d12::AccelStructPool::accel_struct_node& phi::d3d12::AccelStructPool::acquireAccelStruct(handle::accel_struct& out_handle)
{
    unsigned res;
    {
        auto lg = std::lock_guard(mMutex);
        res = mPool.acquire();
    }

    out_handle = {static_cast<handle::index_t>(res)};
    return mPool.get(res);
}

void phi::d3d12::AccelStructPool::internalFree(phi::d3d12::AccelStructPool::accel_struct_node& node)
{
    cc::array const buffers_to_free = {node.buffer_as, node.buffer_scratch, node.buffer_instances};
    mResourcePool->free(buffers_to_free);
}
