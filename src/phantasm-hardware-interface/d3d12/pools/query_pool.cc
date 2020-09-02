#include "query_pool.hh"

phi::handle::query_range phi::d3d12::QueryPool::create(phi::query_type type, unsigned size)
{
    auto lg = std::lock_guard(mMutex);
    int const res_index = getHeap(type).allocate(int(size));
    return IndexToHandle(res_index, type);
}

void phi::d3d12::QueryPool::free(phi::handle::query_range qr)
{
    auto lg = std::lock_guard(mMutex);
    auto const type = HandleToQueryType(qr);
    auto const index = HandleToIndex(qr, type);
    getHeap(type).free(index);
}

void phi::d3d12::QueryPool::initialize(ID3D12Device* device, unsigned num_timestamp, unsigned num_occlusion, unsigned num_pipeline_stats)
{
    CC_ASSERT(num_timestamp < mcIndexOffsetStep && num_occlusion < mcIndexOffsetStep && num_pipeline_stats < mcIndexOffsetStep && "too many queries configured");
    mHeapTimestamps.initialize(device, D3D12_QUERY_HEAP_TYPE_TIMESTAMP, num_timestamp);
    mHeapOcclusion.initialize(device, D3D12_QUERY_HEAP_TYPE_OCCLUSION, num_occlusion);
    mHeapPipelineStats.initialize(device, D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, num_pipeline_stats);
}

void phi::d3d12::QueryPool::destroy()
{
    mHeapTimestamps.destroy();
    mHeapOcclusion.destroy();
    mHeapPipelineStats.destroy();
}
