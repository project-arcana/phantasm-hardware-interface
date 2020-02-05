#pragma once

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/detail/hash.hh>
#include <phantasm-hardware-interface/detail/stable_map.hh>

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
    struct rootsig_key_readonly
    {
        arg::shader_arg_shapes arg_shapes;
        bool has_root_constants;
        root_signature_type type;
    };

    struct rootsig_key
    {
        cc::capped_vector<arg::shader_arg_shape, limits::max_shader_arguments> arg_shapes;
        bool has_root_constants;
        root_signature_type type;

        rootsig_key() = default;
        rootsig_key(rootsig_key_readonly const& ro) : arg_shapes(ro.arg_shapes), has_root_constants(ro.has_root_constants), type(ro.type) {}

        bool operator==(rootsig_key_readonly const& lhs) const noexcept
        {
            return type == lhs.type && arg_shapes == lhs.arg_shapes && has_root_constants == lhs.has_root_constants;
        }
    };

    struct rootsig_hasher
    {
        cc::hash_t operator()(rootsig_key_readonly const& v) const noexcept
        {
            return cc::make_hash(hash::compute(v.arg_shapes), v.type, v.has_root_constants);
        }

        cc::hash_t operator()(rootsig_key const& v) const noexcept
        {
            return cc::make_hash(hash::compute(v.arg_shapes), v.type, v.has_root_constants);
        }
    };

    phi::detail::stable_map<rootsig_key, root_signature, rootsig_hasher> mCache;
};

}
