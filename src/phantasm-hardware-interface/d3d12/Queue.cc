#include "Queue.hh"

#include "common/d3d12_sanitized.hh"
#include "common/native_enum.hh"
#include "common/verify.hh"
#include "common/util.hh"

void phi::d3d12::Queue::initialize(ID3D12Device& device, queue_type type)
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = util::to_native(type);

    PR_D3D12_VERIFY(device.CreateCommandQueue(&queueDesc, PR_COM_WRITE(mQueue)));
    util::set_object_name(mQueue, "type %d queue", util::to_native(type));
}
