#include "Queue.hh"

#include "common/d3d12_sanitized.hh"
#include "common/native_enum.hh"
#include "common/util.hh"
#include "common/verify.hh"

void phi::d3d12::Queue::initialize(ID3D12Device& device, queue_type type)
{
    CC_ASSERT(command_queue == nullptr && "double init");
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = util::to_native(type);

    char const* const queue_literal = util::to_queue_type_literal(queueDesc.Type);

    PHI_D3D12_VERIFY(device.CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&command_queue)));
    util::set_object_name(command_queue, "%s queue", queue_literal);

    PHI_D3D12_VERIFY(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    util::set_object_name(fence, "internal fence for %s queue", queue_literal);
}

void phi::d3d12::Queue::destroy()
{
    PHI_D3D12_SAFE_RELEASE(command_queue);
    PHI_D3D12_SAFE_RELEASE(fence);
}
