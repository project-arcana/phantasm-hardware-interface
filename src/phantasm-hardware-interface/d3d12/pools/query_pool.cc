#include "query_pool.hh"

#include <phantasm-hardware-interface/d3d12/common/verify.hh>

phi::handle::query_range phi::d3d12::QueryPool::create(phi::query_type type, unsigned size)
{
    auto lg = std::lock_guard(mMutex);
    uint64_t const res_index = getHeap(type).allocate(size);
    return IndexToHandle(res_index, type);
}

void phi::d3d12::QueryPool::free(phi::handle::query_range qr)
{
    auto lg = std::lock_guard(mMutex);
    auto const type = HandleToQueryType(qr);
    auto const index = HandleToIndex(qr, type);
    getHeap(type).free(index);
}

void phi::d3d12::QueryPool::initialize(ID3D12Device* device, unsigned num_timestamp, unsigned num_occlusion, unsigned num_pipeline_stats, cc::allocator* static_alloc)
{
    CC_ASSERT(num_timestamp < QP_IndexOffsetStep && num_occlusion < QP_IndexOffsetStep && num_pipeline_stats < QP_IndexOffsetStep && "too many queries configured");
    mHeapTimestamps.initialize(device, D3D12_QUERY_HEAP_TYPE_TIMESTAMP, num_timestamp, static_alloc);
    mHeapOcclusion.initialize(device, D3D12_QUERY_HEAP_TYPE_OCCLUSION, num_occlusion, static_alloc);
    mHeapPipelineStats.initialize(device, D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, num_pipeline_stats, static_alloc);
}

void phi::d3d12::QueryPool::destroy()
{
    mHeapTimestamps.destroy();
    mHeapOcclusion.destroy();
    mHeapPipelineStats.destroy();
}

void phi::d3d12::QueryPageAllocator::initialize(ID3D12Device* device, D3D12_QUERY_HEAP_TYPE type, unsigned max_num_queries, cc::allocator* static_alloc)
{
    mType = type;
    D3D12_QUERY_HEAP_DESC desc = {type, max_num_queries, 0};
    device->CreateQueryHeap(&desc, IID_PPV_ARGS(&mHeap));

    mPageAllocator.initialize(max_num_queries, PA_PageSize, static_alloc);
}

void phi::d3d12::QueryPageAllocator::destroy() { PHI_D3D12_SAFE_RELEASE(mHeap); }
