#pragma once

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>

namespace D3D12MA
{
class Allocator;
class Allocation;
}

namespace phi::d3d12
{
class ResourceAllocator
{
public:
    ResourceAllocator() = default;
    ResourceAllocator(ResourceAllocator const&) = delete;
    ResourceAllocator(ResourceAllocator&&) noexcept = delete;
    ResourceAllocator& operator=(ResourceAllocator const&) = delete;
    ResourceAllocator& operator=(ResourceAllocator&&) noexcept = delete;

public:
    void initialize(ID3D12Device& device);
    void destroy();

    /// allocate a resource, thread safe
    [[nodiscard]] D3D12MA::Allocation* allocate(D3D12_RESOURCE_DESC const& desc,
                                                D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON,
                                                D3D12_CLEAR_VALUE* clear_value = nullptr,
                                                D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT) const;

private:
    D3D12MA::Allocator* mAllocator = nullptr;
    ID3D12Device* mDevice = nullptr;
};
}
