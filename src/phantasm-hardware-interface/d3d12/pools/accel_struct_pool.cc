#include "accel_struct_pool.hh"

#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/log.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/d3d12/common/dxgi_format.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

#include "resource_pool.hh"

phi::handle::accel_struct phi::d3d12::AccelStructPool::createBottomLevelAS(cc::span<const phi::arg::blas_element> elements,
                                                                           accel_struct_build_flags_t flags,
                                                                           accel_struct_prebuild_info* out_prebuild_info)
{
    handle::accel_struct res_handle;
    accel_struct_node& new_node = acquireAccelStruct(res_handle);
    new_node.reset(mDynamicAllocator, unsigned(elements.size()));
    new_node.flags = flags;

    // build the D3D12_RAYTRACING_GEOMETRY_DESCs from the vertex/index buffer pairs
    for (auto const& elem : elements)
    {
        auto const& vert_info = mResourcePool->getBufferInfo(elem.vertex_addr.buffer);
        CC_ASSERT(vert_info.stride > 0 && "vertex buffers used in bottom level accel struct elements must have been created with a specified stride");

        D3D12_RAYTRACING_GEOMETRY_DESC& egeom = new_node.geometries.emplace_back();
        egeom = {};
        egeom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        egeom.Triangles.Transform3x4 = 0;
        egeom.Triangles.VertexBuffer.StartAddress = mResourcePool->getBufferAddrVA(elem.vertex_addr);
        egeom.Triangles.VertexBuffer.StrideInBytes = vert_info.stride;
        egeom.Triangles.VertexCount = elem.num_vertices;
        egeom.Triangles.VertexFormat = util::to_dxgi_format(elem.vertex_pos_format);


        if (elem.index_addr.buffer.is_valid())
        {
            auto const index_stride = mResourcePool->getBufferInfo(elem.index_addr.buffer).stride;
            CC_ASSERT(index_stride > 0 && "index buffers used in bottom level accel struct elements must have been created with a specified stride");

            egeom.Triangles.IndexBuffer = mResourcePool->getBufferAddrVA(elem.index_addr);
            egeom.Triangles.IndexCount = elem.num_indices;
            egeom.Triangles.IndexFormat = index_stride == sizeof(uint16_t) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        }
        else
        {
            egeom.Triangles.IndexBuffer = 0;
            egeom.Triangles.IndexCount = 0;
            egeom.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
        }

        if (elem.transform_addr.buffer.is_valid())
        {
            CC_ASSERT(mResourcePool->isBufferAccessInBounds(elem.transform_addr, sizeof(float[3 * 4])) && "BLAS element transform address OOB");

            egeom.Triangles.Transform3x4 = mResourcePool->getBufferAddrVA(elem.transform_addr);

            CC_ASSERT(phi::util::is_aligned(egeom.Triangles.Transform3x4, D3D12_RAYTRACING_TRANSFORM3X4_BYTE_ALIGNMENT)
                      && "BLAS elem transform address must be aligned to 16B");
        }
        else
        {
            egeom.Triangles.Transform3x4 = 0;
        }

        egeom.Flags = elem.is_opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
    }

    // Assemble the bottom level AS object
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS as_input_info = {};
    as_input_info.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    as_input_info.Flags = util::to_native_accel_struct_build_flags(flags);
    as_input_info.NumDescs = UINT(new_node.geometries.size());
    as_input_info.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    as_input_info.pGeometryDescs = new_node.geometries.data();

    // Query sizes for scratch and result buffers
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info = {};
    mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&as_input_info, &prebuild_info);
    CC_ASSERT(prebuild_info.ResultDataMaxSizeInBytes > 0);

    // Create scratch and result buffers
    new_node.buffer_as = mResourcePool->createBufferInternal(prebuild_info.ResultDataMaxSizeInBytes, 0, true,
                                                             D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "pool BLAS buffer");

    auto const scratchSize = cc::max<UINT64>(prebuild_info.ScratchDataSizeInBytes, prebuild_info.UpdateScratchDataSizeInBytes);
    if (flags & accel_struct_build_flags::no_internal_scratch_buffer)
    {
        new_node.buffer_scratch = handle::null_resource;
    }
    else
    {
        new_node.buffer_scratch = mResourcePool->createBufferInternal(scratchSize, 0, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "pool BLAS scratch");
    }

    if (out_prebuild_info)
    {
        out_prebuild_info->buffer_size_bytes = prebuild_info.ResultDataMaxSizeInBytes;
        out_prebuild_info->required_build_scratch_size_bytes = prebuild_info.ScratchDataSizeInBytes;
        out_prebuild_info->required_update_scratch_size_bytes = prebuild_info.UpdateScratchDataSizeInBytes;
    }

    // PHI_LOG_TRACE("Created BLAS for {} elements, {} B AS, {} B Scratch", elements.size(), prebuild_info.ResultDataMaxSizeInBytes, scratchSize);

    // query AS buffer GPU VA
    new_node.buffer_as_va = mResourcePool->getBufferInfo(new_node.buffer_as).gpu_va;

    return res_handle;
}

phi::handle::accel_struct phi::d3d12::AccelStructPool::createTopLevelAS(unsigned num_instances, accel_struct_build_flags_t flags, accel_struct_prebuild_info* out_prebuild_info)
{
    static_assert(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) == sizeof(accel_struct_instance), "acceleration instance struct sizes mismatch");
    CC_ASSERT(num_instances > 0 && "empty top-level accel_struct not allowed");

    handle::accel_struct res_handle;
    accel_struct_node& new_node = acquireAccelStruct(res_handle);
    new_node.reset(mDynamicAllocator, 0);
    new_node.flags = flags;

    // Assemble the bottom level AS object
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS as_input_info = {};
    as_input_info.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    as_input_info.Flags = util::to_native_accel_struct_build_flags(flags);
    as_input_info.NumDescs = num_instances;
    as_input_info.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    as_input_info.pGeometryDescs = nullptr;

    // Query sizes for scratch and result buffers
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info = {};
    mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&as_input_info, &prebuild_info);
    CC_ASSERT(prebuild_info.ResultDataMaxSizeInBytes > 0);

    // Create result buffer
    new_node.buffer_as = mResourcePool->createBufferInternal(prebuild_info.ResultDataMaxSizeInBytes, 0, true,
                                                             D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "pool TLAS buffer");
    // query GPU VA ("raw native handle" in phi API naming)
    new_node.buffer_as_va = mResourcePool->getBufferInfo(new_node.buffer_as).gpu_va;

    auto const scratchSize = cc::max<UINT64>(prebuild_info.ScratchDataSizeInBytes, prebuild_info.UpdateScratchDataSizeInBytes);
    if (flags & accel_struct_build_flags::no_internal_scratch_buffer)
    {
        new_node.buffer_scratch = handle::null_resource;
    }
    else
    {
        // create scratch buffer
        new_node.buffer_scratch = mResourcePool->createBufferInternal(scratchSize, 0, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "pool TLAS scratch");
    }

    if (out_prebuild_info)
    {
        out_prebuild_info->buffer_size_bytes = prebuild_info.ResultDataMaxSizeInBytes;
        out_prebuild_info->required_build_scratch_size_bytes = prebuild_info.ScratchDataSizeInBytes;
        out_prebuild_info->required_update_scratch_size_bytes = prebuild_info.UpdateScratchDataSizeInBytes;
    }

    return res_handle;
}

void phi::d3d12::AccelStructPool::free(phi::handle::accel_struct as)
{
    if (!as.is_valid())
        return;

    accel_struct_node& freed_node = mPool.get(as._value);
    internalFree(freed_node);
    mPool.release(as._value);
}

void phi::d3d12::AccelStructPool::free(cc::span<const phi::handle::accel_struct> as_span)
{
    for (auto as : as_span)
    {
        free(as);
    }
}

void phi::d3d12::AccelStructPool::initialize(
    ID3D12Device5* device, phi::d3d12::ResourcePool* res_pool, unsigned max_num_accel_structs, cc::allocator* static_alloc, cc::allocator* dynamic_alloc)
{
    CC_ASSERT(mDevice == nullptr && mResourcePool == nullptr && "double init");
    mDevice = device;
    mResourcePool = res_pool;
    mDynamicAllocator = dynamic_alloc;
    mPool.initialize(max_num_accel_structs, static_alloc);
}

void phi::d3d12::AccelStructPool::destroy()
{
    if (mDevice != nullptr)
    {
        auto num_leaks = 0;
        mPool.iterate_allocated_nodes([&](accel_struct_node& leaked_node) {
            ++num_leaks;
            internalFree(leaked_node);
        });

        if (num_leaks > 0)
        {
            PHI_LOG("leaked {} handle::accel_struct object{}", num_leaks, num_leaks == 1 ? "" : "s");
        }
    }
}

phi::d3d12::AccelStructPool::accel_struct_node& phi::d3d12::AccelStructPool::getNode(phi::handle::accel_struct as)
{
    CC_ASSERT(as.is_valid() && "accessed invalid handle::accel_struct");
    return mPool.get(as._value);
}

phi::d3d12::AccelStructPool::accel_struct_node& phi::d3d12::AccelStructPool::acquireAccelStruct(handle::accel_struct& out_handle)
{
    unsigned const res = mPool.acquire();

    out_handle = {static_cast<handle::handle_t>(res)};
    return mPool.get(res);
}

void phi::d3d12::AccelStructPool::internalFree(phi::d3d12::AccelStructPool::accel_struct_node& node)
{
    handle::resource buffers_to_free[] = {node.buffer_as, node.buffer_scratch};
    mResourcePool->free(buffers_to_free);
}

void phi::d3d12::AccelStructPool::accel_struct_node::reset(cc::allocator* dyn_alloc, unsigned num_geom_reserve)
{
    buffer_as_va = 0;
    buffer_as = handle::null_resource;
    buffer_scratch = handle::null_resource;
    flags = {};
    geometries.reset_reserve(dyn_alloc, num_geom_reserve);
}
