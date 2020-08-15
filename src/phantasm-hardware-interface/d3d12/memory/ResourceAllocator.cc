#include "ResourceAllocator.hh"

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/d3d12/common/verify.hh>
#include <phantasm-hardware-interface/d3d12/memory/D3D12MA.hh>

void phi::d3d12::ResourceAllocator::initialize(ID3D12Device& device)
{
    CC_ASSERT(mAllocator == nullptr);

    D3D12MA::ALLOCATOR_DESC allocator_desc = {};
    allocator_desc.Flags = D3D12MA::ALLOCATOR_FLAGS::ALLOCATOR_FLAG_NONE;
    allocator_desc.pDevice = &device;
    allocator_desc.PreferredBlockSize = 0;         // default
    allocator_desc.pAllocationCallbacks = nullptr; // default

    auto const hr = D3D12MA::CreateAllocator(&allocator_desc, &mAllocator);
    PHI_D3D12_ASSERT(hr);

    mDevice = &device;
}

void phi::d3d12::ResourceAllocator::destroy()
{
    // this is not a COM pointer although it looks like one
    if (mAllocator != nullptr)
        mAllocator->Release();
}

D3D12MA::Allocation* phi::d3d12::ResourceAllocator::allocate(const D3D12_RESOURCE_DESC& desc,
                                                             D3D12_RESOURCE_STATES initial_state,
                                                             D3D12_CLEAR_VALUE* clear_value,
                                                             D3D12_HEAP_TYPE heap_type) const
{
    D3D12MA::ALLOCATION_DESC allocation_desc = {};
    allocation_desc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
    allocation_desc.HeapType = heap_type;

    D3D12MA::Allocation* res;
    auto const hr = mAllocator->CreateResource(&allocation_desc, &desc, initial_state, clear_value, &res, __uuidof(ID3D12Resource), nullptr);
    PHI_D3D12_ASSERT_FULL(hr, mDevice);
    return res;
}
