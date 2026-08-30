#pragma once

#include <mutex>

#include <phantasm-hardware-interface/common/page_allocator.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/d3d12/common/shared_com_ptr.hh>
#include <phantasm-hardware-interface/d3d12/fwd.hh>

namespace phi::d3d12
{
/// Unsynchronized
class QueryPageAllocator
{
public:
    enum
    {
        PA_PageSize = 2
    };

public:
    void initialize(ID3D12Device* device, D3D12_QUERY_HEAP_TYPE type, unsigned max_num_queries, cc::allocator* static_alloc);

    void destroy();

    [[nodiscard]] uint64_t allocate(uint64_t num_queries)
    {
        if (num_queries == 0)
            return uint64_t(-1);

        auto const res_page = mPageAllocator.allocate(num_queries);
        CC_RUNTIME_ASSERT(res_page != uint64_t(-1) && "QueryPageAllocator overcommited");
        return res_page;
    }

    void free(uint64_t handle) { mPageAllocator.free(handle); }

    [[nodiscard]] uint64_t getPoolwideIndex(uint64_t handle, uint64_t offset) const
    {
        CC_ASSERT(offset < mPageAllocator.get_allocation_size_in_elements(handle) && "query_range access out of bounds");
        return handle * PA_PageSize + offset;
    }

    [[nodiscard]] uint64_t getNumPages() const { return mPageAllocator.get_num_pages(); }
    ID3D12QueryHeap* getHeap() const { return mHeap; }
    D3D12_QUERY_HEAP_TYPE getNativeType() const { return mType; }

private:
    ID3D12QueryHeap* mHeap;
    phi::page_allocator mPageAllocator;
    D3D12_QUERY_HEAP_TYPE mType;
};

/// Synchronized
class QueryPool
{
public:
    [[nodiscard]] handle::query_range create(query_type type, unsigned size);
    void free(handle::query_range qr);

public:
    enum
    {
        QP_IndexOffsetStep = 1'000'000,
        QP_IndexOffsetTimestamp = QP_IndexOffsetStep * 0,
        QP_IndexOffsetOcclusion = QP_IndexOffsetStep * 1,
        QP_IndexOffsetPipelineStats = QP_IndexOffsetStep * 2,
    };

    static constexpr query_type HandleToQueryType(handle::query_range qr)
    {
        if (qr._value >= QP_IndexOffsetPipelineStats)
            return query_type::pipeline_stats;
        else if (qr._value >= QP_IndexOffsetOcclusion)
            return query_type::occlusion;
        else
            return query_type::timestamp;
    }

    static constexpr handle::query_range IndexToHandle(uint64_t index, query_type type)
    {
        // we rely on underlying values here
        static_assert(int(query_type::timestamp) == 0, "unexpected enum ordering");
        static_assert(int(query_type::occlusion) == 1, "unexpected enum ordering");
        static_assert(int(query_type::pipeline_stats) == 2, "unexpected enum ordering");
        return {uint32_t(index + QP_IndexOffsetStep * int(type))};
    }

    static constexpr uint64_t HandleToIndex(handle::query_range qr, query_type type)
    {
        //
        return qr._value - QP_IndexOffsetStep * uint64_t(type);
    }

    QueryPageAllocator& getHeap(query_type type)
    {
        switch (type)
        {
        case query_type::timestamp:
            return mHeapTimestamps;
        case query_type::occlusion:
            return mHeapOcclusion;
        case query_type::pipeline_stats:
            return mHeapPipelineStats;
        }

        CC_UNREACHABLE("invalid query_type");
    }

    QueryPageAllocator const& getHeap(query_type type) const
    {
        switch (type)
        {
        case query_type::timestamp:
            return mHeapTimestamps;
        case query_type::occlusion:
            return mHeapOcclusion;
        case query_type::pipeline_stats:
            return mHeapPipelineStats;
        }

        CC_UNREACHABLE("invalid query_type");
    }

public:
    // internal API

    void initialize(ID3D12Device* device, unsigned num_timestamp, unsigned num_occlusion, unsigned num_pipeline_stats, cc::allocator* static_alloc);

    void destroy();

    /// returns Query-internal index, query_range with unknown type
    [[nodiscard]] UINT getQuery(handle::query_range qr, unsigned offset, ID3D12QueryHeap*& out_heap, query_type& out_type)
    {
        out_type = HandleToQueryType(qr);
        auto const index = HandleToIndex(qr, out_type);
        auto& heap = getHeap(out_type);
        out_heap = heap.getHeap();
        return heap.getPoolwideIndex(index, offset);
    }

    /// returns Query-internal index, query_range with expected type
    [[nodiscard]] UINT getQuery(handle::query_range qr, query_type type, unsigned offset, ID3D12QueryHeap*& out_heap)
    {
        CC_ASSERT(HandleToQueryType(qr) == type && "unexpected handle::query_range type");
        auto const index = HandleToIndex(qr, type);
        auto& heap = getHeap(type);
        out_heap = heap.getHeap();
        return heap.getPoolwideIndex(index, offset);
    }

private:
    std::mutex mMutex;
    QueryPageAllocator mHeapTimestamps;
    QueryPageAllocator mHeapOcclusion;
    QueryPageAllocator mHeapPipelineStats;
};
} // namespace phi::d3d12
