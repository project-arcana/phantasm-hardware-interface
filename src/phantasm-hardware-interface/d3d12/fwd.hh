#pragma once

#include <phantasm-hardware-interface/detail/detail_fwd.hh>

#include "common/d3d12_fwd.hh"

namespace phi::d3d12
{
class BackendD3D12;

class ShaderViewPool;
class ResourcePool;
class PipelineStateObjectPool;
class AccelStructPool;
class CommandListPool;

class CPUDescriptorLinearAllocator;

struct command_list_translator;

using d3d12_incomplete_state_cache = phi::detail::generic_incomplete_state_cache<D3D12_RESOURCE_STATES>;
}
