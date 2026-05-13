#include "ResourceAllocator.hh"

#ifdef PHI_HAS_OPTICK
#include <optick.h>
#endif

#include <clean-core/allocator.hh>
#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/d3d12/common/verify.hh>
#include <phantasm-hardware-interface/d3d12/memory/D3D12MemAlloc.hh>

void phi::d3d12::ResourceAllocator::initialize(ID3D12Device* pDevice, IDXGIAdapter* pAdapter, cc::allocator* pDynamicAlloc)
{
    CC_ASSERT(mAllocator == nullptr);

    D3D12MA::ALLOCATION_CALLBACKS callbacks = {};
    callbacks.pAllocate = +[](size_t size, size_t align, void* alloc) -> void* { return reinterpret_cast<cc::allocator*>(alloc)->alloc(size, align); };
    callbacks.pFree = +[](void* mem, void* alloc) -> void { reinterpret_cast<cc::allocator*>(alloc)->free(mem); };
    callbacks.pPrivateData = pDynamicAlloc;

    D3D12MA::ALLOCATOR_DESC allocator_desc = {};
    allocator_desc.Flags = D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS;
    allocator_desc.pDevice = pDevice;
    allocator_desc.PreferredBlockSize = 0; // default
    allocator_desc.pAllocationCallbacks = &callbacks;
    allocator_desc.pAdapter = pAdapter;

    auto const hr = D3D12MA::CreateAllocator(&allocator_desc, &mAllocator);
    PHI_D3D12_ASSERT(hr);

    mDevice = pDevice;
}

void phi::d3d12::ResourceAllocator::destroy()
{
    // this is not a COM pointer although it looks like one
    if (mAllocator != nullptr)
    {
        mAllocator->Release();
        mAllocator = nullptr;
    }
}

D3D12MA::Allocation* phi::d3d12::ResourceAllocator::allocate(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initial_state, D3D12_CLEAR_VALUE* clear_value, D3D12_HEAP_TYPE heap_type)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT(); // any significant time spent here is due to locking, serial times are sub-ms
#endif

    D3D12MA::ALLOCATION_DESC allocation_desc = {};
    allocation_desc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
    allocation_desc.HeapType = heap_type;

    D3D12MA::Allocation* res;
    PHI_D3D12_VERIFY_FULL(mAllocator->CreateResource(&allocation_desc, &desc, initial_state, clear_value, &res, __uuidof(ID3D12Resource), nullptr), mDevice);
    return res;
}

phi::allocated_resource_info phi::d3d12::ResourceAllocator::getStats()
{
    D3D12MA::TotalStatistics FullStats = {};
    mAllocator->CalculateStatistics(&FullStats);

    D3D12MA::DetailedStatistics const& Stats = FullStats.Total;

    allocated_resource_info res = {};

    res.num_allocated_blocks = Stats.Stats.BlockCount;
    res.num_allocations = Stats.Stats.AllocationCount;
    res.num_unused_ranges = Stats.UnusedRangeCount;

    res.num_allocated_bytes = Stats.Stats.BlockBytes;
    res.num_unused_bytes = (Stats.Stats.BlockBytes - Stats.Stats.AllocationBytes);

    res.num_bytes_allocations_min = Stats.AllocationSizeMin;
    res.num_bytes_allocations_max = Stats.AllocationSizeMax;

    res.num_bytes_unused_ranges_min = Stats.UnusedRangeSizeMin;
    res.num_bytes_unused_ranges_max = Stats.UnusedRangeSizeMax;

    return res;
}
