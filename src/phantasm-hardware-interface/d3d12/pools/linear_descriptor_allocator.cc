#include "linear_descriptor_allocator.hh"

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>

void phi::d3d12::CPUDescriptorLinearAllocator::initialize(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, unsigned size)
{
    CC_RUNTIME_ASSERT(((type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) || (type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) && "Only use this class for CPU-visible descriptors");

    mNumDescriptors = size;
    mDescriptorSize = device.GetDescriptorHandleIncrementSize(type);
    mNumAllocatedDescriptors = 0;

    D3D12_DESCRIPTOR_HEAP_DESC desc;
    desc.NumDescriptors = mNumDescriptors;
    desc.Type = type;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;
    PR_D3D12_VERIFY(device.CreateDescriptorHeap(&desc, PR_COM_WRITE(mHeap)));
    util::set_object_name(mHeap, "linear cpu desc heap, size %d", int(mNumDescriptors));
    mHandleCPU = mHeap->GetCPUDescriptorHandleForHeapStart();
}

phi::d3d12::resource_view_cpu_only phi::d3d12::CPUDescriptorLinearAllocator::allocate(unsigned num)
{
    auto const res = D3D12_CPU_DESCRIPTOR_HANDLE{mHandleCPU.ptr + mNumAllocatedDescriptors * mDescriptorSize};

    mNumAllocatedDescriptors += num;
    CC_RUNTIME_ASSERT(mNumDescriptors >= mNumAllocatedDescriptors && "CPUDescriptorLinearAllocator full");

    return resource_view_cpu_only{mDescriptorSize, res};
}
