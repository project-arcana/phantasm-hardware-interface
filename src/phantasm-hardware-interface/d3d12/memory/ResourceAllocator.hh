#pragma once

#include <clean-core/fwd.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>

namespace D3D12MA
{
class Allocator;
class Allocation;
} // namespace D3D12MA

namespace phi::d3d12
{
class ResourceAllocator
{
public:
    void initialize(ID3D12Device* device, cc::allocator* dynamic_alloc);
    void destroy();

    /// allocate a resource, thread safe
    [[nodiscard]] D3D12MA::Allocation* allocate(D3D12_RESOURCE_DESC const& desc,
                                                D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON,
                                                D3D12_CLEAR_VALUE* clear_value = nullptr,
                                                D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT);

private:
    D3D12MA::Allocator* mAllocator = nullptr;
    ID3D12Device* mDevice = nullptr;
};
} // namespace phi::d3d12
