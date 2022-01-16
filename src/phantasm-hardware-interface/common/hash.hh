#pragma once

#include <stdint.h>

#include <phantasm-hardware-interface/common/api.hh>
#include <phantasm-hardware-interface/fwd.hh>

namespace phi
{
PHI_API uint64_t ComputeHash(arg::root_signature_description const& rootSignatureDesc);

PHI_API uint64_t ComputeHash(arg::graphics_pipeline_state_description const& psoDesc);

PHI_API uint64_t ComputeHash(arg::compute_pipeline_state_description const& psoDesc);
} // namespace phi
