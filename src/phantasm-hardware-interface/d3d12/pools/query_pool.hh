#pragma once

#include <mutex>

#include <phantasm-hardware-interface/detail/linked_pool.hh>
#include <phantasm-hardware-interface/detail/page_allocator.hh>
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
    using handle_t = int;
    constexpr static int sc_page_size = 2;

public:
    void initialize(ID3D12Device& device, D3D12_QUERY_HEAP_TYPE type, unsigned max_num_queries)
    {
        mType = type;
        D3D12_QUERY_HEAP_DESC desc = {type, max_num_queries, 0};
        device.CreateQueryHeap(&desc, PHI_COM_WRITE(mHeap));

        mPageAllocator.initialize(max_num_queries, sc_page_size);
    }

    void destroy() { mHeap = nullptr; }

    [[nodiscard]] handle_t allocate(int num_queries)
    {
        if (num_queries <= 0)
            return -1;

        auto const res_page = mPageAllocator.allocate(num_queries);
        CC_RUNTIME_ASSERT(res_page != -1 && "QueryPageAllocator overcommited");
        return res_page;
    }

    void free(handle_t handle) { mPageAllocator.free(handle); }

    [[nodiscard]] UINT getPoolwideIndex(handle_t handle, unsigned offset) const
    {
        CC_ASSERT(int(offset) < mPageAllocator.get_allocation_size_in_elements(handle) && "query_range access out of bounds");
        return handle * sc_page_size + offset;
    }

    [[nodiscard]] int getNumPages() const { return mPageAllocator.get_num_pages(); }
    ID3D12QueryHeap* getHeap() const { return mHeap.get(); }
    D3D12_QUERY_HEAP_TYPE getNativeType() const { return mType; }

private:
    shared_com_ptr<ID3D12QueryHeap> mHeap;
    phi::detail::page_allocator mPageAllocator;
    D3D12_QUERY_HEAP_TYPE mType;
};

/// Synchronized
class QueryPool
{
public:
    [[nodiscard]] handle::query_range create(query_type type, unsigned size);
    void free(handle::query_range qr);

public:
    static constexpr int mcIndexOffsetStep = 1'000'000;
    static constexpr int mcIndexOffsetTimestamp = mcIndexOffsetStep * 0;
    static constexpr int mcIndexOffsetOcclusion = mcIndexOffsetStep * 1;
    static constexpr int mcIndexOffsetPipelineStats = mcIndexOffsetStep * 2;

    static constexpr query_type HandleToQueryType(handle::query_range qr)
    {
        if (qr._value >= mcIndexOffsetPipelineStats)
            return query_type::pipeline_stats;
        else if (qr._value >= mcIndexOffsetOcclusion)
            return query_type::occlusion;
        else
            return query_type::timestamp;
    }

    static constexpr handle::query_range IndexToHandle(int index, query_type type)
    {
        // we rely on underlying values here
        static_assert(int(query_type::timestamp) == 0, "unexpected enum ordering");
        static_assert(int(query_type::occlusion) == 1, "unexpected enum ordering");
        static_assert(int(query_type::pipeline_stats) == 2, "unexpected enum ordering");
        return {unsigned(index + mcIndexOffsetStep * int(type))};
    }

    static constexpr int HandleToIndex(handle::query_range qr, query_type type)
    {
        //
        return qr._value - mcIndexOffsetStep * int(type);
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

    void initialize(ID3D12Device& device, unsigned num_timestamp, unsigned num_occlusion, unsigned num_pipeline_stats);

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
}
