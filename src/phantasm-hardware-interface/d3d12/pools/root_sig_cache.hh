#pragma once

#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/common/container/stable_map.hh>
#include <phantasm-hardware-interface/common/hash.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>
#include <phantasm-hardware-interface/d3d12/root_signature.hh>

namespace phi::d3d12
{
// Persistent cache for root signatures
// Unsynchronized, only used inside of pso pool
class RootSignatureCache
{
public:
    void initialize(unsigned max_num_root_sigs, cc::allocator* alloc);
    void destroy();

    // receive an existing root signature matching the shape, or create a new one
    // returns a pointer which remains stable
    [[nodiscard]] root_signature* getOrCreate(ID3D12Device& device, arg::root_signature_description const& desc, root_signature_type type);

private:
    struct rootsig_key_readonly
    {
        arg::root_signature_description const* pDescription;
        root_signature_type type;
    };

    struct rootsig_key
    {
        arg::root_signature_description description;
        root_signature_type type;

        rootsig_key() = default;
        rootsig_key(rootsig_key_readonly const& ro) : description(*ro.pDescription), type(ro.type) {}

        bool operator==(rootsig_key_readonly const& lhs) const noexcept
        {
            return type == lhs.type && !memcmp(&description, lhs.pDescription, sizeof(arg::root_signature_description));
        }
    };

    struct rootsig_hasher
    {
        uint64_t operator()(rootsig_key_readonly const& v) const noexcept { return cc::hash_combine(ComputeHash(*v.pDescription), (uint8_t)v.type); }
        uint64_t operator()(rootsig_key const& v) const noexcept { return cc::hash_combine(ComputeHash(v.description), (uint8_t)v.type); }
    };

    phi::detail::stable_map<rootsig_key, root_signature, rootsig_hasher> mCache;
};


// Persistent cache for command signatures that depend upon root signatures
// strictly typed per usecase, currently only has the "Draw ID" comsig for draw_indexed_with_id
class CommandSignatureCache
{
public:
    void initialize(uint32_t maxNumComSigs, cc::allocator* alloc);
    void destroy();

    ID3D12CommandSignature* getOrCreateDrawIDComSig(ID3D12Device* pDevice, root_signature const* pRootSig);

private:
    phi::detail::stable_map<root_signature const*, ID3D12CommandSignature*> mCache;
};
} // namespace phi::d3d12
