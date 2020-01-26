#include "shader_view_pool.hh"

#include <phantasm-hardware-interface/d3d12/common/dxgi_format.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

#include "resource_pool.hh"

void phi::d3d12::DescriptorPageAllocator::initialize(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, unsigned num_descriptors, unsigned page_size)
{
    mPageAllocator.initialize(num_descriptors, page_size);
    mDescriptorSize = device.GetDescriptorHandleIncrementSize(type);

    D3D12_DESCRIPTOR_HEAP_DESC desc;
    desc.NumDescriptors = UINT(num_descriptors);
    desc.Type = type;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;
    PHI_D3D12_VERIFY(device.CreateDescriptorHeap(&desc, PHI_COM_WRITE(mHeap)));
    util::set_object_name(mHeap, "desc page allocator, type %d, size %d", int(type), int(num_descriptors));

    mHeapStartCPU = mHeap->GetCPUDescriptorHandleForHeapStart();
    mHeapStartGPU = mHeap->GetGPUDescriptorHandleForHeapStart();
}

phi::handle::shader_view phi::d3d12::ShaderViewPool::create(cc::span<resource_view const> srvs,
                                                                            cc::span<resource_view const> uavs,
                                                                            cc::span<sampler_config const> samplers)
{
    auto const srv_uav_size = int(srvs.size() + uavs.size());
    DescriptorPageAllocator::handle_t srv_uav_alloc;
    DescriptorPageAllocator::handle_t sampler_alloc;
    unsigned pool_index;
    {
        auto lg = std::lock_guard(mMutex);
        srv_uav_alloc = mSRVUAVAllocator.allocate(srv_uav_size);
        sampler_alloc = mSamplerAllocator.allocate(static_cast<int>(samplers.size()));
        pool_index = mPool.acquire();
    }

    // Populate the data entry and fill out descriptors
    {
        auto& new_node = mPool.get(pool_index);
        new_node.sampler_alloc_handle = sampler_alloc;
        new_node.srv_uav_alloc_handle = srv_uav_alloc;
        new_node.resources.clear();

        // Create the descriptors in-place
        // SRVs and UAVs
        if (srv_uav_alloc != -1)
        {
            auto const srv_uav_cpu_base = mSRVUAVAllocator.getCPUStart(srv_uav_alloc);
            auto srv_uav_desc_index = 0u;

            for (auto const& srv : srvs)
            {
                new_node.resources.push_back(srv.resource);

                ID3D12Resource* const raw_resource = mResourcePool->getRawResource(srv.resource);
                auto const cpu_handle = mSRVUAVAllocator.incrementToIndex(srv_uav_cpu_base, srv_uav_desc_index++);
                // Create a SRV based on the shader_view_element
                auto const srv_desc = util::create_srv_desc(srv, raw_resource);
                mDevice->CreateShaderResourceView(srv.dimension == resource_view_dimension::raytracing_accel_struct ? nullptr : raw_resource, &srv_desc, cpu_handle);
            }

            for (auto const& uav : uavs)
            {
                new_node.resources.push_back(uav.resource);

                ID3D12Resource* const raw_resource = mResourcePool->getRawResource(uav.resource);
                auto const cpu_handle = mSRVUAVAllocator.incrementToIndex(srv_uav_cpu_base, srv_uav_desc_index++);

                // Create a UAV (without a counter resource) based on the shader_view_element
                auto const uav_desc = util::create_uav_desc(uav);
                mDevice->CreateUnorderedAccessView(raw_resource, nullptr, &uav_desc, cpu_handle);
            }

            new_node.srv_uav_handle = mSRVUAVAllocator.getGPUStart(srv_uav_alloc);
        }
        else
        {
            new_node.srv_uav_handle = {};
        }

        // Samplers
        if (sampler_alloc != -1)
        {
            auto const sampler_cpu_base = mSamplerAllocator.getCPUStart(sampler_alloc);
            auto sampler_desc_index = 0u;

            for (auto const& sampler_conf : samplers)
            {
                auto const cpu_handle = mSamplerAllocator.incrementToIndex(sampler_cpu_base, sampler_desc_index++);

                // Create a Sampler based on the shader configuration
                auto const sampler_desc = util::create_sampler_desc(sampler_conf);
                mDevice->CreateSampler(&sampler_desc, cpu_handle);
            }

            new_node.sampler_handle = mSamplerAllocator.getGPUStart(sampler_alloc);
        }
        else
        {
            new_node.sampler_handle = {};
        }

        new_node.num_srvs = cc::uint16(srvs.size());
        new_node.num_uavs = cc::uint16(uavs.size());
    }

    return {static_cast<handle::index_t>(pool_index)};
}

void phi::d3d12::ShaderViewPool::free(phi::handle::shader_view sv)
{
    auto& data = mPool.get(static_cast<unsigned>(sv.index));
    data.resources.clear();
    {
        auto lg = std::lock_guard(mMutex);
        mSRVUAVAllocator.free(data.srv_uav_alloc_handle);
        mSamplerAllocator.free(data.sampler_alloc_handle);
        mPool.release(static_cast<unsigned>(sv.index));
    }
}

void phi::d3d12::ShaderViewPool::free(cc::span<const phi::handle::shader_view> svs)
{
    auto lg = std::lock_guard(mMutex);
    for (auto sv : svs)
    {
        auto& data = mPool.get(static_cast<unsigned>(sv.index));
        data.resources.clear();
        mSRVUAVAllocator.free(data.srv_uav_alloc_handle);
        mSamplerAllocator.free(data.sampler_alloc_handle);
        mPool.release(static_cast<unsigned>(sv.index));
    }
}

void phi::d3d12::ShaderViewPool::initialize(ID3D12Device* device, phi::d3d12::ResourcePool* res_pool, unsigned num_shader_views, unsigned num_srvs_uavs, unsigned num_samplers)
{
    mDevice = device;
    mResourcePool = res_pool;
    mSRVUAVAllocator.initialize(*device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, num_srvs_uavs);
    mSamplerAllocator.initialize(*device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, num_samplers);
    mPool.initialize(num_shader_views);
}

void phi::d3d12::ShaderViewPool::destroy()
{
    // nothing, the heaps themselves are being destroyed
}
