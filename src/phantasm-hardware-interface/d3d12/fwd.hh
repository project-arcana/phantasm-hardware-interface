#pragma once

#include <phantasm-hardware-interface/common/common_fwd.hh>

#include "common/d3d12_fwd.hh"

namespace phi::d3d12
{
class BackendD3D12;

class ShaderViewPool;
class ResourcePool;
class PipelineStateObjectPool;
class AccelStructPool;
class CommandListPool;
class QueryPool;

class CPUDescriptorLinearAllocator;

struct command_list_translator;
struct incomplete_state_cache;
}
