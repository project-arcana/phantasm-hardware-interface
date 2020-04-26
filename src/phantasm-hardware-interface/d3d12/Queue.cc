#include "Queue.hh"

#include "common/d3d12_sanitized.hh"
#include "common/native_enum.hh"
#include "common/util.hh"
#include "common/verify.hh"

namespace
{
char const* to_queue_type_literal(phi::queue_type t)
{
    switch (t)
    {
    case phi::queue_type::direct:
        return "direct";
    case phi::queue_type::copy:
        return "copy";
    case phi::queue_type::compute:
        return "compute";
    }

    return "unknown_type";
}
}

void phi::d3d12::Queue::initialize(ID3D12Device& device, queue_type type)
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = util::to_native(type);

    PHI_D3D12_VERIFY(device.CreateCommandQueue(&queueDesc, PHI_COM_WRITE(mQueue)));
    util::set_object_name(mQueue, "%s queue", to_queue_type_literal(type));
}
