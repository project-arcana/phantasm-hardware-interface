#pragma once

#include <cstdint>

#include <mutex>

#include <clean-core/array.hh>
#include <clean-core/assertf.hh>
#include <clean-core/atomic_linked_pool.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/span.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/common/page_allocator.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/d3d12/fwd.hh>

namespace phi::d3d12
{
// A page allocator for variable-sized descriptors
//
// Descriptors are used for shader arguments, and play two roles there:
//     - Single CBV root descriptor
//         This one should ideally come from a different, freelist allocator since by nature its always of size 1
//     - Shader view
//          n contiguous SRVs and m contiguous UAVs
//         This allocator is intended for this scenario
//         We likely do not want to keep additional descriptors around,
//         Just allocate here once and directly device.make... the descriptors in-place
//         As both are the same type, we just need a single of these allocators
//
// We might have to add defragmentation at some point, which would probably require an additional indirection
// Lookup and free is O(1), allocate is O(#pages), but still fast and skipping blocks
// Unsynchronized
class DescriptorPageAllocator
{
public:
    using handle_t = int32_t;

public:
    void initialize(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t num_descriptors, uint32_t page_size, cc::allocator* static_alloc, bool bShaderVisible);

    void destroy();

    [[nodiscard]] handle_t allocate(int32_t num_descriptors)
    {
        if (num_descriptors <= 0)
            return -1;

        auto const res_page = mPageAllocator.allocate((uint64_t)num_descriptors);
        CC_RUNTIME_ASSERTF(res_page != uint64_t(-1), "DescriptorPageAllocator overcommitted! Reached limit of {} {}\nIncrease the corresponding maximum in the PHI backend config",
                           mPageAllocator.get_num_elements(), mDescriptorType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? "SRVs/UAVs/CBVs" : "Samplers");

        mNumLiveDescriptors += mPageAllocator.get_allocation_size_in_elements(res_page);

        return (int32_t)res_page;
    }

    void free(handle_t handle)
    {
        mNumLiveDescriptors -= mPageAllocator.get_allocation_size_in_elements((uint64_t)handle);
        mPageAllocator.free((uint64_t)handle);
    }

public:
    D3D12_CPU_DESCRIPTOR_HANDLE getCPUStart(handle_t handle) const
    {
        CC_ASSERT(handle != -1);

        // index = page index * page size
        auto const index = handle * mPageAllocator.get_page_size();
        return D3D12_CPU_DESCRIPTOR_HANDLE{mHeapStartCPU.ptr + SIZE_T(index) * SIZE_T(mDescriptorSize)};
    }

    D3D12_GPU_DESCRIPTOR_HANDLE getGPUStart(handle_t handle) const
    {
        CC_ASSERT(handle != -1);
        CC_ASSERT(mHeapStartGPU.ptr != 0 && "Attempted to GPU access a heap which is not GPU-visible");

        // index = page index * page size
        auto const index = handle * mPageAllocator.get_page_size();
        return D3D12_GPU_DESCRIPTOR_HANDLE{mHeapStartGPU.ptr + SIZE_T(index) * SIZE_T(mDescriptorSize)};
    }

    uint32_t getNumDescriptorsInAllocation(handle_t handle) const
    {
        CC_ASSERT(handle != -1);

        return uint32_t(mPageAllocator.get_allocation_size_in_elements(handle));
    }

    int32_t getNumLiveDescriptors() const { return mNumLiveDescriptors; }

    int32_t getMaxNumDescriptors() const { return (int32_t)mPageAllocator.get_num_elements(); }

    float getAllocatedLiveDescriptorRatio() const { return mNumLiveDescriptors / (float)mPageAllocator.get_num_elements(); }

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE incrementToIndex(D3D12_CPU_DESCRIPTOR_HANDLE desc, uint32_t i) const
    {
        desc.ptr += i * SIZE_T(mDescriptorSize);
        return desc;
    }

    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE incrementToIndex(D3D12_GPU_DESCRIPTOR_HANDLE desc, uint32_t i) const
    {
        desc.ptr += i * SIZE_T(mDescriptorSize);
        return desc;
    }

    int32_t getNumPages() const { return mPageAllocator.get_num_pages(); }

    ID3D12DescriptorHeap* getHeap() const { return mHeap; }

private:
    ID3D12DescriptorHeap* mHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE mHeapStartCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE mHeapStartGPU;
    phi::page_allocator mPageAllocator;
    int32_t mNumLiveDescriptors = 0;

public:
    uint32_t mDescriptorSize = 0;
    D3D12_DESCRIPTOR_HEAP_TYPE mDescriptorType;
};

class ResourcePool;

/// The high-level allocator for shader views
/// Synchronized
class ShaderViewPool
{
public:
    // frontend-facing API

    handle::shader_view createEmpty(uint32_t num_srvs, uint32_t num_uavs, uint32_t num_samplers, bool bStaging);

    handle::shader_view create(cc::span<resource_view const> srvs, cc::span<resource_view const> uavs, cc::span<sampler_config const> samplers);

    void writeShaderViewSRVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> srvs);

    void writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs);

    void writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers);

    void copyShaderViewSRVs(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors);

    void copyShaderViewUAVs(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors);

    void copyShaderViewSamplers(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors);

    void free(handle::shader_view sv);
    void free(cc::span<handle::shader_view const> svs);

    allocated_descriptor_info queryAllocatedNumDescriptors();

public:
    // internal API

    void initialize(ID3D12Device* device,
                    ResourcePool* res_pool,
                    phi::d3d12::AccelStructPool* as_pool,
                    uint32_t num_shader_views,
                    uint32_t num_srvs_uavs,
                    uint32_t num_samplers,
                    cc::allocator* static_alloc);
    void destroy();

    D3D12_GPU_DESCRIPTOR_HANDLE getSRVUAVGPUHandle(handle::shader_view sv) const
    {
        // cached fastpath
        return internalGet(sv).srv_uav_handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE getSamplerGPUHandle(handle::shader_view sv) const
    {
        // cached fastpath
        return internalGet(sv).sampler_handle;
    }

    bool hasSRVsUAVs(handle::shader_view sv) const { return internalGet(sv).srv_uav_alloc_handle != -1; }
    bool hasSamplers(handle::shader_view sv) const { return internalGet(sv).sampler_alloc_handle != -1; }

    cc::array<ID3D12DescriptorHeap*, 2> getGPURelevantHeaps() const { return {mSRVUAVAllocator.getHeap(), mSamplerAllocator.getHeap()}; }

private:
    struct shader_view_data
    {
        // pre-constructed gpu handles
        D3D12_GPU_DESCRIPTOR_HANDLE srv_uav_handle = {};
        D3D12_GPU_DESCRIPTOR_HANDLE sampler_handle = {};

        // Descriptor allocator handles
        DescriptorPageAllocator::handle_t srv_uav_alloc_handle = -1;
        DescriptorPageAllocator::handle_t sampler_alloc_handle = -1;
        uint32_t numSRVs = 0;
        uint32_t numUAVs = 0;
        bool bIsStaging = false;
    };

private:
    shader_view_data const& internalGet(handle::shader_view res) const
    {
        CC_ASSERT(res.is_valid() && "invalid shader_view handle");
        return mPool.get(res._value);
    }

    shader_view_data& internalGet(handle::shader_view res)
    {
        CC_ASSERT(res.is_valid() && "invalid shader_view handle");
        return mPool.get(res._value);
    }

    void writeSRV(D3D12_CPU_DESCRIPTOR_HANDLE handle, resource_view const& srv);

    void writeUAV(D3D12_CPU_DESCRIPTOR_HANDLE handle, resource_view const& uav);

    void writeSampler(D3D12_CPU_DESCRIPTOR_HANDLE handle, sampler_config const& sampler);

private:
    // non-owning
    ID3D12Device* mDevice = nullptr;
    ResourcePool* mResourcePool = nullptr;
    AccelStructPool* mAccelStructPool = nullptr;

    cc::atomic_linked_pool<shader_view_data> mPool;

    DescriptorPageAllocator mSRVUAVAllocator;
    DescriptorPageAllocator mSamplerAllocator;

    DescriptorPageAllocator mStagingSRVUAVAllocator;
    DescriptorPageAllocator mStagingSamplerAllocator;

    std::mutex mMutex;
};
} // namespace phi::d3d12
