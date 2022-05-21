#include "hash.hh"

#include <clean-core/hash.hh>
#include <clean-core/hash_combine.hh>
#include <clean-core/stringhash.hh>
#include <clean-core/xxHash.hh>

#include <phantasm-hardware-interface/common/sse_hash.hh>

uint64_t phi::ComputeHash(arg::root_signature_description const& rootSignatureDesc)
{
    //
    return util::sse_hash_type(&rootSignatureDesc);
}

uint64_t phi::ComputeHash(arg::graphics_pipeline_state_description const& psoDesc)
{
    uint64_t psoHash = cc::hash_combine(           //
        util::sse_hash_type(&psoDesc.config),      //
        util::sse_hash_type(&psoDesc.framebuffer), //
        util::sse_hash_type(&psoDesc.root_signature));

    // vertex attributes
    for (phi::vertex_attribute_info const& attribute : psoDesc.vertices.attributes)
    {
        psoHash = cc::hash_combine(psoHash, cc::stringhash(attribute.semantic_name), cc::make_hash(attribute.offset, attribute.fmt, attribute.vertex_buffer_i));
    }
    psoHash = cc::hash_combine(psoHash, phi::util::sse_hash_data(psoDesc.vertices.vertex_sizes_bytes, sizeof(psoDesc.vertices.vertex_sizes_bytes)));

    // shaders
    for (phi::arg::graphics_shader const& shader : psoDesc.shader_binaries)
    {
        psoHash = cc::hash_combine(psoHash, cc::make_hash(shader.stage), cc::hash_xxh3(cc::span(shader.binary.data, shader.binary.size), 0u));
    }

    return psoHash;
}

uint64_t phi::ComputeHash(arg::compute_pipeline_state_description const& psoDesc)
{
    uint64_t psoHash = util::sse_hash_type(&psoDesc.root_signature);

    // shader
    psoHash = cc::hash_combine(psoHash, cc::hash_xxh3(cc::span(psoDesc.shader.data, psoDesc.shader.size), 0u));
    return psoHash;
}

uint64_t phi::ComputeHash(arg::texture_description const& texDesc)
{
    //
    return util::sse_hash_type(&texDesc);
}

uint64_t phi::ComputeHash(arg::buffer_description const& bufDesc)
{
    //
    return util::sse_hash_type(&bufDesc);
}

uint64_t phi::ComputeHash(arg::resource_description const& resDesc)
{
    switch (resDesc.type)
    {
    case arg::resource_description::e_resource_buffer:
        return cc::hash_combine(uint64_t(resDesc.type), util::sse_hash_type(&resDesc.info_buffer));
    case arg::resource_description::e_resource_texture:
        return cc::hash_combine(uint64_t(resDesc.type), util::sse_hash_type(&resDesc.info_texture));
    }

    return 0;
}
