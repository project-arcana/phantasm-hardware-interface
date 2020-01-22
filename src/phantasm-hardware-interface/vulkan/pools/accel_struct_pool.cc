#include "accel_struct_pool.hh"

#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/vulkan/common/log.hh>
#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/util.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

#include "resource_pool.hh"

namespace
{
void query_accel_struct_buffer_sizes(VkDevice device, VkAccelerationStructureNV raw_as, VkDeviceSize& out_accel_struct_buf_size, VkDeviceSize& out_scratch_buf_size)
{
    VkAccelerationStructureMemoryRequirementsInfoNV mem_req_info = {};
    mem_req_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
    mem_req_info.pNext = nullptr;
    mem_req_info.accelerationStructure = raw_as;
    mem_req_info.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;

    VkMemoryRequirements2 mem_req;
    vkGetAccelerationStructureMemoryRequirementsNV(device, &mem_req_info, &mem_req);

    // size of the acceleration structure itself
    out_accel_struct_buf_size = mem_req.memoryRequirements.size;

    // scratch buffer, maximum of the sizes for build and update
    mem_req_info.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
    vkGetAccelerationStructureMemoryRequirementsNV(device, &mem_req_info, &mem_req);
    auto scratch_build_size = mem_req.memoryRequirements.size;

    mem_req_info.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV;
    vkGetAccelerationStructureMemoryRequirementsNV(device, &mem_req_info, &mem_req);
    auto scratch_update_size = mem_req.memoryRequirements.size;

    out_scratch_buf_size = cc::max(scratch_build_size, scratch_update_size);
}

}

pr::backend::handle::accel_struct pr::backend::vk::AccelStructPool::createBottomLevelAS(cc::span<const pr::backend::arg::blas_element> elements,
                                                                                        accel_struct_build_flags_t flags)
{
    cc::vector<VkGeometryNV> element_geometries;
    element_geometries.reserve(elements.size());

    // build the VkGeometryNVs from the vertex/index buffer pairs
    for (auto const& elem : elements)
    {
        auto const& vert_info = mResourcePool->getBufferInfo(elem.vertex_buffer);

        VkGeometryNV& egeom = element_geometries.emplace_back();
        egeom = {};
        egeom.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
        egeom.pNext = nullptr;
        egeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
        egeom.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
        egeom.geometry.triangles.vertexData = vert_info.raw_buffer;
        egeom.geometry.triangles.vertexOffset = 0 * vert_info.stride;
        egeom.geometry.triangles.vertexCount = elem.num_vertices;
        egeom.geometry.triangles.vertexStride = vert_info.stride;
        egeom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;

        if (elem.index_buffer.is_valid())
        {
            auto const index_stride = mResourcePool->getBufferInfo(elem.index_buffer).stride;

            egeom.geometry.triangles.indexData = mResourcePool->getRawBuffer(elem.index_buffer);
            egeom.geometry.triangles.indexCount = elem.num_indices;
            egeom.geometry.triangles.indexOffset = index_stride * 0;
            egeom.geometry.triangles.indexType = index_stride == sizeof(uint16_t) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        }
        else
        {
            egeom.geometry.triangles.indexData = nullptr;
            egeom.geometry.triangles.indexCount = 0;
            egeom.geometry.triangles.indexOffset = 0;
            egeom.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_NV;
        }

        egeom.geometry.aabbs.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
        egeom.flags = elem.is_opaque ? VK_GEOMETRY_OPAQUE_BIT_NV : 0;
    }

    // Assemble the bottom level AS object
    VkAccelerationStructureCreateInfoNV as_create_info = {};
    as_create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
    as_create_info.pNext = nullptr;
    as_create_info.info = {};
    as_create_info.info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
    as_create_info.info.pNext = nullptr;
    as_create_info.info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
    as_create_info.info.flags = util::to_native_flags(flags);
    as_create_info.info.instanceCount = 0;
    as_create_info.info.geometryCount = static_cast<uint32_t>(element_geometries.size());
    as_create_info.info.pGeometries = element_geometries.data();
    as_create_info.compactedSize = 0;

    VkAccelerationStructureNV raw_as = nullptr;
    PR_VK_VERIFY_SUCCESS(vkCreateAccelerationStructureNV(mDevice, &as_create_info, nullptr, &raw_as));
    util::set_object_name(mDevice, raw_as, "pool bottom-level accel struct s%u", static_cast<unsigned>(element_geometries.size()));

    // Allocate AS and scratch buffers in the required sizes
    VkDeviceSize buffer_size_as = 0, buffer_size_scratch = 0;
    query_accel_struct_buffer_sizes(mDevice, raw_as, buffer_size_as, buffer_size_scratch);

    auto const buffer_as = mResourcePool->createBufferInternal(buffer_size_as, 0, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV);
    auto const buffer_scratch = mResourcePool->createBufferInternal(buffer_size_scratch, 0, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV);

    // Bind the AS buffer's memory to the AS
    VkBindAccelerationStructureMemoryInfoNV bind_mem_info = {};
    bind_mem_info.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
    bind_mem_info.pNext = nullptr;
    bind_mem_info.accelerationStructure = raw_as;
    bind_mem_info.memory = mResourcePool->getRawDeviceMemory(buffer_as);
    bind_mem_info.memoryOffset = 0;
    bind_mem_info.deviceIndexCount = 0;
    bind_mem_info.pDeviceIndices = nullptr;

    PR_VK_VERIFY_SUCCESS(vkBindAccelerationStructureMemoryNV(mDevice, 1, &bind_mem_info));

    auto const res = acquireAccelStruct(raw_as, flags, buffer_as, buffer_scratch);
    moveGeometriesToAS(res, cc::move(element_geometries));
    return res;
}

pr::backend::handle::accel_struct pr::backend::vk::AccelStructPool::createTopLevelAS(unsigned num_instances)
{
    VkAccelerationStructureCreateInfoNV as_create_info = {};
    as_create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
    as_create_info.pNext = nullptr;
    as_create_info.info = {};
    as_create_info.info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
    as_create_info.info.pNext = nullptr;
    as_create_info.info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
    as_create_info.info.flags = 0;
    as_create_info.info.instanceCount = static_cast<uint32_t>(num_instances);
    as_create_info.info.geometryCount = 0;
    as_create_info.info.pGeometries = nullptr;
    as_create_info.compactedSize = 0;

    VkAccelerationStructureNV raw_as = nullptr;
    PR_VK_VERIFY_SUCCESS(vkCreateAccelerationStructureNV(mDevice, &as_create_info, nullptr, &raw_as));
    util::set_object_name(mDevice, raw_as, "pool top-level accel struct s%u", num_instances);

    VkDeviceSize buffer_size_as = 0, buffer_size_scratch = 0;
    query_accel_struct_buffer_sizes(mDevice, raw_as, buffer_size_as, buffer_size_scratch);

    auto const buffer_as = mResourcePool->createBufferInternal(buffer_size_as, 0, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV);
    auto const buffer_scratch = mResourcePool->createBufferInternal(buffer_size_scratch, 0, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV);

    auto const buffer_size_instances = sizeof(accel_struct_geometry_instance) * num_instances;
    auto const buffer_instances = mResourcePool->createMappedBufferInternal(buffer_size_instances, 0, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV);

    // Bind the AS buffer's memory to the AS
    VkBindAccelerationStructureMemoryInfoNV bind_mem_info = {};
    bind_mem_info.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
    bind_mem_info.pNext = nullptr;
    bind_mem_info.accelerationStructure = raw_as;
    bind_mem_info.memory = mResourcePool->getRawDeviceMemory(buffer_as);
    bind_mem_info.memoryOffset = 0;
    bind_mem_info.deviceIndexCount = 0;
    bind_mem_info.pDeviceIndices = nullptr;

    PR_VK_VERIFY_SUCCESS(vkBindAccelerationStructureMemoryNV(mDevice, 1, &bind_mem_info));

    return acquireAccelStruct(raw_as, {}, buffer_as, buffer_scratch, buffer_instances);
}

void pr::backend::vk::AccelStructPool::free(pr::backend::handle::accel_struct as)
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

void pr::backend::vk::AccelStructPool::free(cc::span<const pr::backend::handle::accel_struct> as_span)
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

void pr::backend::vk::AccelStructPool::initialize(VkDevice device, pr::backend::vk::ResourcePool* res_pool, unsigned max_num_accel_structs)
{
    CC_ASSERT(mDevice == nullptr && mResourcePool == nullptr && "double init");
    mDevice = device;
    mResourcePool = res_pool;
    mPool.initialize(max_num_accel_structs);
}

void pr::backend::vk::AccelStructPool::destroy()
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

pr::backend::vk::AccelStructPool::accel_struct_node& pr::backend::vk::AccelStructPool::getNode(pr::backend::handle::accel_struct as)
{
    CC_ASSERT(as.is_valid());
    return mPool.get(static_cast<unsigned>(as.index));
}

pr::backend::handle::accel_struct pr::backend::vk::AccelStructPool::acquireAccelStruct(VkAccelerationStructureNV raw_as,
                                                                                       accel_struct_build_flags_t flags,
                                                                                       handle::resource buffer_as,
                                                                                       handle::resource buffer_scratch,
                                                                                       handle::resource buffer_instances)
{
    unsigned res;
    {
        auto lg = std::lock_guard(mMutex);
        res = mPool.acquire();
    }

    accel_struct_node& new_node = mPool.get(res);
    new_node.raw_as = raw_as;
    new_node.raw_as_handle = 0;
    new_node.buffer_as = buffer_as;
    new_node.buffer_scratch = buffer_scratch;
    new_node.buffer_instances = buffer_instances;
    new_node.flags = flags;
    new_node.geometries.clear();

    PR_VK_VERIFY_SUCCESS(vkGetAccelerationStructureHandleNV(mDevice, raw_as, sizeof(new_node.raw_as_handle), &new_node.raw_as_handle));

    if (new_node.buffer_instances.is_valid())
    {
        new_node.buffer_instances_map = mResourcePool->getMappedMemory(new_node.buffer_instances);
    }
    else
    {
        new_node.buffer_instances_map = nullptr;
    }

    return {static_cast<handle::index_t>(res)};
}

void pr::backend::vk::AccelStructPool::moveGeometriesToAS(pr::backend::handle::accel_struct as, cc::vector<VkGeometryNV>&& geometries)
{
    CC_ASSERT(as.is_valid());
    mPool.get(static_cast<unsigned>(as.index)).geometries = cc::move(geometries);
}

void pr::backend::vk::AccelStructPool::internalFree(pr::backend::vk::AccelStructPool::accel_struct_node& node)
{
    cc::array const buffers_to_free = {node.buffer_as, node.buffer_scratch, node.buffer_instances};
    mResourcePool->free(buffers_to_free);

    vkDestroyAccelerationStructureNV(mDevice, node.raw_as, nullptr);
}
