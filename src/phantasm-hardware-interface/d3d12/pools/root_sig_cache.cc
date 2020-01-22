#include "root_sig_cache.hh"

#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/detail/hash.hh>

namespace
{
char const* get_root_sig_type_literal(pr::backend::d3d12::root_signature_type type)
{
    using rt = pr::backend::d3d12::root_signature_type;
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

}

void pr::backend::d3d12::RootSignatureCache::initialize(unsigned max_num_root_sigs) { mCache.initialize(max_num_root_sigs); }

void pr::backend::d3d12::RootSignatureCache::destroy() { reset(); }

pr::backend::d3d12::root_signature* pr::backend::d3d12::RootSignatureCache::getOrCreate(ID3D12Device& device,
                                                                                        arg::shader_argument_shapes arg_shapes,
                                                                                        bool has_root_constants,
                                                                                        root_signature_type type)
{
    auto const hash = cc::make_hash(hash::compute(arg_shapes), type, has_root_constants);

    auto* const lookup = mCache.look_up(hash);
    if (lookup != nullptr)
        return lookup;
    else
    {
        auto* const insertion = mCache.insert(hash, root_signature{});
        initialize_root_signature(*insertion, device, arg_shapes, has_root_constants, type);
        util::set_object_name(insertion->raw_root_sig, "cached %s root sig %zx", get_root_sig_type_literal(type), hash);

        return insertion;
    }
}

void pr::backend::d3d12::RootSignatureCache::reset()
{
    mCache.iterate_elements([](root_signature& root_sig) { root_sig.raw_root_sig->Release(); });
    mCache.clear();
}
