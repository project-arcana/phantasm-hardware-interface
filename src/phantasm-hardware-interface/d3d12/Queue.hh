#pragma once

#include <phantasm-hardware-interface/types.hh>

#include "common/d3d12_fwd.hh"
#include "common/shared_com_ptr.hh"

namespace phi::d3d12
{
class Queue
{
public:
    void initialize(ID3D12Device *device, queue_type type);
    void destroy();

    ID3D12CommandQueue* command_queue = nullptr;
    ID3D12Fence* fence = nullptr;
};

}
