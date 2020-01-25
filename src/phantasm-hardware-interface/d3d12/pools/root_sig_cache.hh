#pragma once

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/detail/cache_map.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>
#include <phantasm-hardware-interface/d3d12/root_signature.hh>

namespace phi::d3d12
{
/// Persistent cache for root signatures
/// Unsynchronized, only used inside of pso pool
class RootSignatureCache
{
public:
    void initialize(unsigned max_num_root_sigs);
    void destroy();

    /// receive an existing root signature matching the shape, or create a new one
    /// returns a pointer which remains stable
    [[nodiscard]] root_signature* getOrCreate(ID3D12Device& device, arg::shader_arg_shapes arg_shapes, bool has_root_constants, root_signature_type type);

    /// destroys all elements inside, and clears the map
    void reset();

private:
    phi::detail::cache_map<root_signature> mCache;
};

}
