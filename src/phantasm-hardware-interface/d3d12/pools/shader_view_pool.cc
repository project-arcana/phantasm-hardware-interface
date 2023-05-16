#include "shader_view_pool.hh"

#include <phantasm-hardware-interface/d3d12/common/dxgi_format.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

#include "accel_struct_pool.hh"
#include "resource_pool.hh"

void phi::d3d12::DescriptorPageAllocator::initialize(
    ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t num_descriptors, uint32_t page_size, cc::allocator* static_alloc, bool bShaderVisible)
{
    mPageAllocator.initialize(num_descriptors, page_size, static_alloc);
    mDescriptorSize = device.GetDescriptorHandleIncrementSize(type);
    mDescriptorType = type;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = UINT(num_descriptors);
    desc.Type = type;
    desc.Flags = bShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;
    PHI_D3D12_VERIFY(device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mHeap)));
    util::set_object_name(mHeap, "%sdesc page allocator, type %d, size %d", bShaderVisible ? "" : "staging ", int(type), int(num_descriptors));

    mHeapStartCPU = mHeap->GetCPUDescriptorHandleForHeapStart();
    mHeapStartGPU = mHeap->GetGPUDescriptorHandleForHeapStart();
}

void phi::d3d12::DescriptorPageAllocator::destroy()
{
    if (mHeap != nullptr)
    {
        mHeap->Release();
        mHeap = nullptr;
    }
}

phi::handle::shader_view phi::d3d12::ShaderViewPool::createEmpty(uint32_t num_srvs, uint32_t num_uavs, uint32_t num_samplers, bool bStaging)
{
    DescriptorPageAllocator::handle_t srv_uav_alloc = -1;
    DescriptorPageAllocator::handle_t sampler_alloc = -1;

    DescriptorPageAllocator* const pAllocSRVUAV = bStaging ? &mStagingSRVUAVAllocator : &mSRVUAVAllocator;
    DescriptorPageAllocator* const pAllocSamplers = bStaging ? &mStagingSamplerAllocator : &mSamplerAllocator;

    {
        auto lg = std::lock_guard(mMutex);
        srv_uav_alloc = pAllocSRVUAV->allocate(int(num_srvs + num_uavs));
        sampler_alloc = pAllocSamplers->allocate(int(num_samplers));
    }

    CC_RUNTIME_ASSERTF(!mPool.is_full(),
                       "Reached limit for shader_views, increase max_num_shader_views in the PHI backend config\n"
                       "Current limit: {}",
                       mPool.max_size());

    auto const pool_index = mPool.acquire();

    auto& new_node = mPool.get(pool_index);
    new_node = {};
    new_node.sampler_alloc_handle = sampler_alloc;
    new_node.srv_uav_alloc_handle = srv_uav_alloc;
    new_node.numSRVs = num_srvs;
    new_node.numUAVs = num_uavs;
    new_node.bIsStaging = bStaging;

    if (!bStaging)
    {
        if (srv_uav_alloc != -1)
        {
            new_node.srv_uav_handle = mSRVUAVAllocator.getGPUStart(srv_uav_alloc);
            CC_ASSERT(new_node.srv_uav_handle.ptr != 0 && "Failed to get new SRV/UAV shader view handle");
        }

        if (sampler_alloc != -1)
        {
            new_node.sampler_handle = mSamplerAllocator.getGPUStart(sampler_alloc);
            CC_ASSERT(new_node.sampler_handle.ptr != 0 && "Failed to get new Sampler shader view handle");
        }
    }

    return {pool_index};
}

phi::handle::shader_view phi::d3d12::ShaderViewPool::create(cc::span<resource_view const> srvs, cc::span<resource_view const> uavs, cc::span<sampler_config const> samplers)
{
    auto const res = createEmpty(uint32_t(srvs.size()), uint32_t(uavs.size()), uint32_t(samplers.size()), false);
    auto const& new_node = mPool.get(res._value);

    // fill out descriptors

    // Create the descriptors in-place
    // SRVs and UAVs
    if (new_node.srv_uav_alloc_handle != -1)
    {
        auto const srv_uav_cpu_base = mSRVUAVAllocator.getCPUStart(new_node.srv_uav_alloc_handle);
        auto srv_uav_desc_index = 0u;

        for (auto const& srv : srvs)
        {
            auto const cpu_handle = mSRVUAVAllocator.incrementToIndex(srv_uav_cpu_base, srv_uav_desc_index++);
            writeSRV(cpu_handle, srv);
        }

        for (auto const& uav : uavs)
        {
            auto const cpu_handle = mSRVUAVAllocator.incrementToIndex(srv_uav_cpu_base, srv_uav_desc_index++);
            writeUAV(cpu_handle, uav);
        }
    }

    // Samplers
    if (new_node.sampler_alloc_handle != -1)
    {
        auto const sampler_cpu_base = mSamplerAllocator.getCPUStart(new_node.sampler_alloc_handle);
        auto sampler_desc_index = 0u;

        for (auto const& sampler_conf : samplers)
        {
            auto const cpu_handle = mSamplerAllocator.incrementToIndex(sampler_cpu_base, sampler_desc_index++);
            writeSampler(cpu_handle, sampler_conf);
        }
    }

    return res;
}

void phi::d3d12::ShaderViewPool::writeShaderViewSRVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> srvs)
{
    auto const& node = internalGet(sv);
    CC_ASSERT(node.srv_uav_alloc_handle != -1 && "writing resource view to shader_view without SRV/UAV slots");
    CC_ASSERT(srvs.size() + offset <= node.numSRVs && "writeShaderViewSRVs: write OOB");

    DescriptorPageAllocator* const pAllocSRVUAV = node.bIsStaging ? &mStagingSRVUAVAllocator : &mSRVUAVAllocator;

    auto const descriptorBase = pAllocSRVUAV->getCPUStart(node.srv_uav_alloc_handle);
    for (auto i = 0u; i < srvs.size(); ++i)
    {
        auto const cpuHandle = pAllocSRVUAV->incrementToIndex(descriptorBase, offset + i);
        writeSRV(cpuHandle, srvs[i]);
    }
}

void phi::d3d12::ShaderViewPool::writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs)
{
    auto const& node = internalGet(sv);
    CC_ASSERT(node.srv_uav_alloc_handle != -1 && "writing resource view to shader_view without SRV/UAV slots");
    CC_ASSERT(uavs.size() + offset <= node.numUAVs && "writeShaderViewUAVs: write OOB");

    DescriptorPageAllocator* const pAllocSRVUAV = node.bIsStaging ? &mStagingSRVUAVAllocator : &mSRVUAVAllocator;

    auto const descriptorBase = pAllocSRVUAV->getCPUStart(node.srv_uav_alloc_handle);
    offset += node.numSRVs; // apply SRV offset (SRVs and UAVs are in the same allocation, contiguous)
    for (auto i = 0u; i < uavs.size(); ++i)
    {
        auto const cpuHandle = pAllocSRVUAV->incrementToIndex(descriptorBase, offset + i);
        writeUAV(cpuHandle, uavs[i]);
    }
}

void phi::d3d12::ShaderViewPool::writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers)
{
    auto const& node = internalGet(sv);
    CC_ASSERT(node.sampler_alloc_handle != -1 && "writing resource view to shader_view without SRV/UAV slots");

    DescriptorPageAllocator* const pAllocSamplers = node.bIsStaging ? &mStagingSamplerAllocator : &mSamplerAllocator;

    uint32_t const maxNumDescriptors = pAllocSamplers->getNumDescriptorsInAllocation(node.sampler_alloc_handle);
    CC_ASSERT(samplers.size() + offset <= maxNumDescriptors && "writeShaderViewSamplers: write OOB");

    auto const sampler_base = pAllocSamplers->getCPUStart(node.sampler_alloc_handle);

    for (auto i = 0u; i < samplers.size(); ++i)
    {
        auto const cpu_handle = pAllocSamplers->incrementToIndex(sampler_base, offset + i);
        writeSampler(cpu_handle, samplers[i]);
    }
}

void phi::d3d12::ShaderViewPool::copyShaderViewSRVs(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors)
{
    auto const& nodeDest = internalGet(hDest);
    CC_ASSERT(nodeDest.srv_uav_alloc_handle != -1 && "Copying SRVs to shader_view without SRV slots");
    CC_ASSERT(numDescriptors + offsetDest <= nodeDest.numSRVs && "copyShaderViewSRVs: Copy OOB in destination");
    DescriptorPageAllocator* const pAllocSRVUAV = nodeDest.bIsStaging ? &mStagingSRVUAVAllocator : &mSRVUAVAllocator;
    auto const handleDest = pAllocSRVUAV->incrementToIndex(pAllocSRVUAV->getCPUStart(nodeDest.srv_uav_alloc_handle), offsetDest);

    auto const& nodeSrc = internalGet(hSrc);
    CC_ASSERT(nodeSrc.srv_uav_alloc_handle != -1 && "Copying SRVs from shader_view without SRV slots");
    CC_ASSERT(numDescriptors + offsetSrc <= nodeSrc.numSRVs && "copyShaderViewSRVs: Copy OOB in source");
    CC_ASSERT(nodeSrc.bIsStaging && "copyShaderViewSRVs: Source must be a staging shader view");
    auto const handleSrc = mStagingSRVUAVAllocator.incrementToIndex(mStagingSRVUAVAllocator.getCPUStart(nodeSrc.srv_uav_alloc_handle), offsetSrc);

    mDevice->CopyDescriptorsSimple(numDescriptors, handleDest, handleSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void phi::d3d12::ShaderViewPool::copyShaderViewUAVs(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors)
{
    auto const& nodeDest = internalGet(hDest);
    CC_ASSERT(nodeDest.srv_uav_alloc_handle != -1 && "Copying UAVs to shader_view without UAV slots");
    CC_ASSERT(numDescriptors + offsetDest <= nodeDest.numUAVs && "copyShaderViewUAVs: Copy OOB in destination");
    DescriptorPageAllocator* const pAllocSRVUAV = nodeDest.bIsStaging ? &mStagingSRVUAVAllocator : &mSRVUAVAllocator;
    auto const handleDest = pAllocSRVUAV->incrementToIndex(pAllocSRVUAV->getCPUStart(nodeDest.srv_uav_alloc_handle), offsetDest + nodeDest.numSRVs);

    auto const& nodeSrc = internalGet(hSrc);
    CC_ASSERT(nodeSrc.srv_uav_alloc_handle != -1 && "Copying UAVs from shader_view without UAV slots");
    CC_ASSERT(numDescriptors + offsetSrc <= nodeSrc.numUAVs && "copyShaderViewUAVs: Copy OOB in source");
    CC_ASSERT(nodeSrc.bIsStaging && "copyShaderViewUAVs: Source must be a staging shader view");
    auto const handleSrc
        = mStagingSRVUAVAllocator.incrementToIndex(mStagingSRVUAVAllocator.getCPUStart(nodeSrc.srv_uav_alloc_handle), offsetSrc + nodeDest.numSRVs);

    mDevice->CopyDescriptorsSimple(numDescriptors, handleDest, handleSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void phi::d3d12::ShaderViewPool::copyShaderViewSamplers(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors)
{
    auto const& nodeDest = internalGet(hDest);
    CC_ASSERT(nodeDest.sampler_alloc_handle != -1 && "Copying Samplers to shader_view without Sampler slots");
    CC_ASSERT(numDescriptors + offsetDest <= mSamplerAllocator.getNumDescriptorsInAllocation(nodeDest.sampler_alloc_handle)
              && "copyShaderViewSamplers: Copy OOB in destination");
    DescriptorPageAllocator* const pAllocSamplers = nodeDest.bIsStaging ? &mStagingSamplerAllocator : &mSamplerAllocator;
    auto const handleDest = pAllocSamplers->incrementToIndex(pAllocSamplers->getCPUStart(nodeDest.sampler_alloc_handle), offsetDest);

    auto const& nodeSrc = internalGet(hSrc);
    CC_ASSERT(nodeSrc.sampler_alloc_handle != -1 && "Copying Samplers from shader_view without Sampler slots");
    CC_ASSERT(nodeSrc.bIsStaging && "copyShaderViewSamplers: Source must be a staging shader view");
    CC_ASSERT(numDescriptors + offsetSrc <= mStagingSamplerAllocator.getNumDescriptorsInAllocation(nodeSrc.sampler_alloc_handle)
              && "copyShaderViewSamplers: Copy OOB in source");
    auto const handleSrc = mStagingSamplerAllocator.incrementToIndex(mStagingSamplerAllocator.getCPUStart(nodeSrc.sampler_alloc_handle), offsetSrc);

    mDevice->CopyDescriptorsSimple(numDescriptors, handleDest, handleSrc, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void phi::d3d12::ShaderViewPool::free(phi::handle::shader_view sv)
{
    auto& node = mPool.get(uint32_t(sv._value));
    DescriptorPageAllocator* const pAllocSRVUAV = node.bIsStaging ? &mStagingSRVUAVAllocator : &mSRVUAVAllocator;
    DescriptorPageAllocator* const pAllocSamplers = node.bIsStaging ? &mStagingSamplerAllocator : &mSamplerAllocator;
    {
        auto lg = std::lock_guard(mMutex);
        pAllocSRVUAV->free(node.srv_uav_alloc_handle);
        pAllocSamplers->free(node.sampler_alloc_handle);
    }
    mPool.release(uint32_t(sv._value));
}

void phi::d3d12::ShaderViewPool::free(cc::span<const phi::handle::shader_view> svs)
{
    auto lg = std::lock_guard(mMutex);
    for (auto sv : svs)
    {
        if (!sv.is_valid())
            continue;

        auto& node = mPool.get(uint32_t(sv._value));
        DescriptorPageAllocator* const pAllocSRVUAV = node.bIsStaging ? &mStagingSRVUAVAllocator : &mSRVUAVAllocator;
        DescriptorPageAllocator* const pAllocSamplers = node.bIsStaging ? &mStagingSamplerAllocator : &mSamplerAllocator;
        pAllocSRVUAV->free(node.srv_uav_alloc_handle);
        pAllocSamplers->free(node.sampler_alloc_handle);
        mPool.release(uint32_t(sv._value));
    }
}

void phi::d3d12::ShaderViewPool::initialize(ID3D12Device* device,
                                            phi::d3d12::ResourcePool* res_pool,
                                            phi::d3d12::AccelStructPool* as_pool,
                                            uint32_t num_shader_views,
                                            uint32_t num_srvs_uavs,
                                            uint32_t num_samplers,
                                            cc::allocator* static_alloc)
{
    CC_ASSERT(mDevice == nullptr && "double init");
    mDevice = device;
    mResourcePool = res_pool;
    mAccelStructPool = as_pool;

    mSRVUAVAllocator.initialize(*device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, num_srvs_uavs, 8, static_alloc, true);
    mSamplerAllocator.initialize(*device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, num_samplers, 8, static_alloc, true);

    mStagingSRVUAVAllocator.initialize(*device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, num_srvs_uavs, 8, static_alloc, false);
    mStagingSamplerAllocator.initialize(*device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, num_samplers, 8, static_alloc, false);

    mPool.initialize(num_shader_views, static_alloc);
}

void phi::d3d12::ShaderViewPool::destroy()
{
    mPool.destroy();
    mSRVUAVAllocator.destroy();
    mSamplerAllocator.destroy();
    mStagingSRVUAVAllocator.destroy();
    mStagingSamplerAllocator.destroy();
}

void phi::d3d12::ShaderViewPool::writeSRV(D3D12_CPU_DESCRIPTOR_HANDLE handle, resource_view const& srv)
{
    // the GPU VA if this is an accel struct
    D3D12_GPU_VIRTUAL_ADDRESS accelstruct_va = UINT64(-1);
    bool const isAccelStruct = srv.dimension == resource_view_dimension::raytracing_accel_struct;
    if (isAccelStruct)
    {
        accelstruct_va = mAccelStructPool->getNode(srv.accel_struct_info.accel_struct).buffer_as_va;
    }

    auto const srv_desc = util::create_srv_desc(srv, accelstruct_va);

    // the raw resource, or none if this is an accel struct
    ID3D12Resource* const raw_resource = isAccelStruct ? nullptr : mResourcePool->getRawResource(srv.resource);

    mDevice->CreateShaderResourceView(raw_resource, &srv_desc, handle);
}

void phi::d3d12::ShaderViewPool::writeUAV(D3D12_CPU_DESCRIPTOR_HANDLE handle, resource_view const& uav)
{
    // Create a UAV (without a counter resource) based on the shader_view_element
    ID3D12Resource* const raw_resource = mResourcePool->getRawResource(uav.resource);
    auto const uav_desc = util::create_uav_desc(uav);
    mDevice->CreateUnorderedAccessView(raw_resource, nullptr, &uav_desc, handle);
}

void phi::d3d12::ShaderViewPool::writeSampler(D3D12_CPU_DESCRIPTOR_HANDLE handle, sampler_config const& sampler)
{
    // Create a Sampler based on the shader configuration
    auto const sampler_desc = util::create_sampler_desc(sampler);
    mDevice->CreateSampler(&sampler_desc, handle);
}
