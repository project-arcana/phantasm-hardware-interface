#include "root_sig_cache.hh"

#include <phantasm-hardware-interface/d3d12/common/util.hh>

namespace
{
char const* get_root_sig_type_literal(phi::d3d12::root_signature_type type)
{
    using rt = phi::d3d12::root_signature_type;
    switch (type)
    {
    case rt::graphics:
        return "graphics";
    case rt::compute:
        return "compute";
    case rt::raytrace_local:
        return "raytrace_local";
    case rt::raytrace_global:
        return "raytrace_global";
    }
    return "unknown";
}

} // namespace

void phi::d3d12::RootSignatureCache::initialize(unsigned max_num_root_sigs, cc::allocator* alloc) { mCache.initialize(max_num_root_sigs, alloc); }

void phi::d3d12::RootSignatureCache::destroy()
{
    mCache.iterate_elements([](root_signature& root_sig) { root_sig.raw_root_sig->Release(); });
    mCache.reset();
}

phi::d3d12::root_signature* phi::d3d12::RootSignatureCache::getOrCreate(ID3D12Device& device, arg::shader_arg_shapes arg_shapes, bool has_root_constants, root_signature_type type)
{
    auto const readonly_key = rootsig_key_readonly{arg_shapes, has_root_constants, type};

    root_signature& val = mCache[readonly_key];
    if (val.raw_root_sig == nullptr)
    {
        initialize_root_signature(val, device, arg_shapes, has_root_constants, type);
        util::set_object_name(val.raw_root_sig, "cached %s root sig", get_root_sig_type_literal(type));
    }

    return &val;
}

void phi::d3d12::CommandSignatureCache::initialize(uint32_t maxNumComSigs, cc::allocator* alloc)
{
    //
    mCache.initialize(maxNumComSigs, alloc);
}

void phi::d3d12::CommandSignatureCache::destroy()
{
    mCache.iterate_elements([](ID3D12CommandSignature*& pComSig) { pComSig->Release(); });
    mCache.reset();
}

ID3D12CommandSignature* phi::d3d12::CommandSignatureCache::getOrCreateDrawIDComSig(ID3D12Device* pDevice, root_signature const* pRootSig)
{
    ID3D12CommandSignature*& val = mCache[pRootSig];
    if (val == nullptr)
    {
        val = createCommandSignatureForDrawIndexedWithID(pDevice, pRootSig->raw_root_sig);
    }

    return val;
}
