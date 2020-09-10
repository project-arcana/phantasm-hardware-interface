#include "query_pool.hh"

phi::handle::query_range phi::vk::QueryPool::create(phi::query_type type, unsigned size)
{
    auto lg = std::lock_guard(mMutex);
    int const res_index = getHeap(type).allocate(int(size));
    return IndexToHandle(res_index, type);
}

void phi::vk::QueryPool::free(phi::handle::query_range qr)
{
    auto lg = std::lock_guard(mMutex);
    auto const type = HandleToQueryType(qr);
    auto const index = HandleToIndex(qr, type);
    getHeap(type).free(index);
}

void phi::vk::QueryPool::initialize(VkDevice device, unsigned num_timestamp, unsigned num_occlusion, unsigned num_pipeline_stats, cc::allocator* static_alloc)
{
    CC_ASSERT(num_timestamp < mcIndexOffsetStep && num_occlusion < mcIndexOffsetStep && num_pipeline_stats < mcIndexOffsetStep && "too many queries configured");
    mHeapTimestamps.initialize(device, VK_QUERY_TYPE_TIMESTAMP, num_timestamp, static_alloc);
    mHeapOcclusion.initialize(device, VK_QUERY_TYPE_OCCLUSION, num_occlusion, static_alloc);
    mHeapPipelineStats.initialize(device, VK_QUERY_TYPE_PIPELINE_STATISTICS, num_pipeline_stats, static_alloc);
}

void phi::vk::QueryPool::destroy(VkDevice dev)
{
    mHeapTimestamps.destroy(dev);
    mHeapOcclusion.destroy(dev);
    mHeapPipelineStats.destroy(dev);
}
