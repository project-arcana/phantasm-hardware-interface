#include "shader_view_pool.hh"

#include <phantasm-hardware-interface/d3d12/common/dxgi_format.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

#include "accel_struct_pool.hh"
#include "resource_pool.hh"

void phi::d3d12::DescriptorPageAllocator::initialize(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, unsigned num_descriptors, unsigned page_size, cc::allocator* static_alloc)
{
    mPageAllocator.initialize(num_descriptors, page_size, static_alloc);
    mDescriptorSize = device.GetDescriptorHandleIncrementSize(type);
    mDescriptorType = type;

    D3D12_DESCRIPTOR_HEAP_DESC desc;
    desc.NumDescriptors = UINT(num_descriptors);
    desc.Type = type;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;
    PHI_D3D12_VERIFY(device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mHeap)));
    util::set_object_name(mHeap, "desc page allocator, type %d, size %d", int(type), int(num_descriptors));

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

phi::handle::shader_view phi::d3d12::ShaderViewPool::createEmpty(uint32_t num_srvs_uavs, uint32_t num_samplers)
{
    DescriptorPageAllocator::handle_t srv_uav_alloc;
    DescriptorPageAllocator::handle_t sampler_alloc;

    {
        auto lg = std::lock_guard(mMutex);
        srv_uav_alloc = mSRVUAVAllocator.allocate(int(num_srvs_uavs));
        sampler_alloc = mSamplerAllocator.allocate(int(num_samplers));
    }

    auto const pool_index = mPool.acquire();

    auto& new_node = mPool.get(pool_index);
    new_node.sampler_alloc_handle = sampler_alloc;
    new_node.srv_uav_alloc_handle = srv_uav_alloc;

    if (srv_uav_alloc != -1)
    {
        new_node.srv_uav_handle = mSRVUAVAllocator.getGPUStart(srv_uav_alloc);
    }
    else
    {
        new_node.srv_uav_handle = {};
    }

    if (sampler_alloc != -1)
    {
        new_node.sampler_handle = mSamplerAllocator.getGPUStart(sampler_alloc);
    }
    else
    {
        new_node.sampler_handle = {};
    }

    return {pool_index};
}

phi::handle::shader_view phi::d3d12::ShaderViewPool::create(cc::span<resource_view const> srvs, cc::span<resource_view const> uavs, cc::span<sampler_config const> samplers)
{
    auto const res = createEmpty(uint32_t(srvs.size() + uavs.size()), uint32_t(samplers.size()));
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

    auto const srv_uav_cpu_base = mSRVUAVAllocator.getCPUStart(node.srv_uav_alloc_handle);

    for (auto i = 0u; i < srvs.size(); ++i)
    {
        auto const cpu_handle = mSRVUAVAllocator.incrementToIndex(srv_uav_cpu_base, offset + i);
        writeSRV(cpu_handle, srvs[i]);
    }
}

void phi::d3d12::ShaderViewPool::writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs)
{
    auto const& node = internalGet(sv);
    CC_ASSERT(node.srv_uav_alloc_handle != -1 && "writing resource view to shader_view without SRV/UAV slots");

    auto const srv_uav_cpu_base = mSRVUAVAllocator.getCPUStart(node.srv_uav_alloc_handle);

    for (auto i = 0u; i < uavs.size(); ++i)
    {
        auto const cpu_handle = mSRVUAVAllocator.incrementToIndex(srv_uav_cpu_base, offset + i);
        writeUAV(cpu_handle, uavs[i]);
    }
}

void phi::d3d12::ShaderViewPool::writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers)
{
    auto const& node = internalGet(sv);
    CC_ASSERT(node.sampler_alloc_handle != -1 && "writing resource view to shader_view without SRV/UAV slots");

    auto const sampler_base = mSamplerAllocator.getCPUStart(node.sampler_alloc_handle);

    for (auto i = 0u; i < samplers.size(); ++i)
    {
        auto const cpu_handle = mSamplerAllocator.incrementToIndex(sampler_base, offset + i);
        writeSampler(cpu_handle, samplers[i]);
    }
}

void phi::d3d12::ShaderViewPool::free(phi::handle::shader_view sv)
{
    auto& data = mPool.get(unsigned(sv._value));
    {
        auto lg = std::lock_guard(mMutex);
        mSRVUAVAllocator.free(data.srv_uav_alloc_handle);
        mSamplerAllocator.free(data.sampler_alloc_handle);
    }
    mPool.release(unsigned(sv._value));
}

void phi::d3d12::ShaderViewPool::free(cc::span<const phi::handle::shader_view> svs)
{
    auto lg = std::lock_guard(mMutex);
    for (auto sv : svs)
    {
        if (!sv.is_valid())
            continue;

        auto& data = mPool.get(unsigned(sv._value));
        mSRVUAVAllocator.free(data.srv_uav_alloc_handle);
        mSamplerAllocator.free(data.sampler_alloc_handle);
        mPool.release(unsigned(sv._value));
    }
}

void phi::d3d12::ShaderViewPool::initialize(ID3D12Device* device,
                                            phi::d3d12::ResourcePool* res_pool,
                                            phi::d3d12::AccelStructPool* as_pool,
                                            unsigned num_shader_views,
                                            unsigned num_srvs_uavs,
                                            unsigned num_samplers,
                                            cc::allocator* static_alloc)
{
    CC_ASSERT(mDevice == nullptr && "double init");
    mDevice = device;
    mResourcePool = res_pool;
    mAccelStructPool = as_pool;
    mSRVUAVAllocator.initialize(*device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, num_srvs_uavs, 8, static_alloc);
    mSamplerAllocator.initialize(*device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, num_samplers, 8, static_alloc);
    mPool.initialize(num_shader_views, static_alloc);
}

void phi::d3d12::ShaderViewPool::destroy()
{
    mPool.destroy();
    mSRVUAVAllocator.destroy();
    mSamplerAllocator.destroy();
}

void phi::d3d12::ShaderViewPool::writeSRV(D3D12_CPU_DESCRIPTOR_HANDLE handle, resource_view const& srv)
{
    // the GPU VA if this is an accel struct
    D3D12_GPU_VIRTUAL_ADDRESS accelstruct_va = UINT64(-1);
    if (srv.dimension == resource_view_dimension::raytracing_accel_struct)
    {
        accelstruct_va = mAccelStructPool->getNode(srv.accel_struct_info.accel_struct).buffer_as_va;
    }

    auto const srv_desc = util::create_srv_desc(srv, accelstruct_va);

    // the raw resource, or none if this is an accel struct
    ID3D12Resource* const raw_resource
        = srv.dimension == resource_view_dimension::raytracing_accel_struct ? nullptr : mResourcePool->getRawResource(srv.resource);

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
