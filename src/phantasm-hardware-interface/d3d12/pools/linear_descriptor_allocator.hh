#pragma once

#include <phantasm-hardware-interface/d3d12/common/d3d12_sanitized.hh>
#include <phantasm-hardware-interface/d3d12/common/shared_com_ptr.hh>

namespace phi::d3d12
{
/// There are two places where "resource views" are API exposed:
/// handle::shader_view
///     (SRVs + UAVs for shaders)
///
/// cmd::begin_render_pass
///     (RTVs + DSV for render targets)
///
/// However within this command, there's nothing but the handle::resource
/// and the "how to view" info. We create small RTV + DSV heaps
/// per recording thread, which act as linear allocators and create the
/// descriptors (not GPU-visible) on the fly
/// CPUDescriptorLinearAllocator is the type of these heaps.
///
/// Jesse Natalie: CPU-only descriptors have ZERO lifetime requirements and can be invalidated before
/// the command list is even closed. This simplifies management for the linear allocators.
///
struct resource_view_cpu_only
{
    resource_view_cpu_only() = default;
    resource_view_cpu_only(unsigned desc_size, D3D12_CPU_DESCRIPTOR_HANDLE cpu) : _descriptor_size(desc_size), _handle_cpu(cpu) {}

    D3D12_CPU_DESCRIPTOR_HANDLE get_index(unsigned i) const { return D3D12_CPU_DESCRIPTOR_HANDLE{_handle_cpu.ptr + i * _descriptor_size}; }
    D3D12_CPU_DESCRIPTOR_HANDLE const& get_start() const { return _handle_cpu; }

    bool is_valid() const { return _descriptor_size != 0; }

private:
    unsigned _descriptor_size = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE _handle_cpu;
};

class CPUDescriptorLinearAllocator
{
public:
    void initialize(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, unsigned size);

    CPUDescriptorLinearAllocator() = default;
    CPUDescriptorLinearAllocator(CPUDescriptorLinearAllocator const&) = delete;
    CPUDescriptorLinearAllocator(CPUDescriptorLinearAllocator&&) = delete;

    [[nodiscard]] resource_view_cpu_only allocate(unsigned num);

    void reset() { mNumAllocatedDescriptors = 0; }

    ID3D12DescriptorHeap* getHeap() const { return mHeap; }

private:
    shared_com_ptr<ID3D12DescriptorHeap> mHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE mHandleCPU;
    unsigned mDescriptorSize = 0;
    unsigned mNumDescriptors = 0;
    unsigned mNumAllocatedDescriptors = 0;
};
}
