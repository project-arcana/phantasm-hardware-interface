#include "pso_pool.hh"

#include <clean-core/alloc_array.hh>
#include <clean-core/alloc_vector.hh>
#include <clean-core/utility.hh>

#include <clean-core/native/wchar_conversion.hh>

#include <phantasm-hardware-interface/detail/byte_util.hh>
#include <phantasm-hardware-interface/detail/log.hh>

#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>
#include <phantasm-hardware-interface/d3d12/pipeline_state.hh>

#include "resource_pool.hh"

namespace
{
constexpr uint32_t gc_d3d12_is_raytracing_pso_bit = (uint32_t(1) << 31);


struct text_buffer
{
    wchar_t* buf = nullptr;
    size_t num_chars = 0;
    size_t cursor = 0;

    void init(wchar_t* buf, size_t num_chars)
    {
        this->buf = buf;
        this->num_chars = num_chars;
    }

    wchar_t const* write_string(char const* str)
    {
        if (!str)
            return nullptr;

        CC_ASSERT(cursor < num_chars && "text buffer full");
        unsigned num_written = cc::char_to_widechar(cc::span{buf + cursor, num_chars - cursor}, str);
        wchar_t const* const res = buf + cursor;
        cursor += num_written;
        return res;
    }
};
}

phi::handle::pipeline_state phi::d3d12::PipelineStateObjectPool::createPipelineState(phi::arg::vertex_format vertex_format,
                                                                                     phi::arg::framebuffer_config const& framebuffer_format,
                                                                                     phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                                     bool has_root_constants,
                                                                                     phi::arg::graphics_shaders shader_stages,
                                                                                     const phi::pipeline_config& primitive_config)
{
    root_signature* root_sig;
    unsigned pool_index;
    // Do things requiring synchronization first
    {
        auto lg = std::lock_guard(mMutex);
        root_sig = mRootSigCache.getOrCreate(*mDevice, shader_arg_shapes, has_root_constants, root_signature_type::graphics);
        pool_index = mPool.acquire();
    }

    // Populate new node
    pso_node& new_node = mPool.get(pool_index);
    new_node.associated_root_sig = root_sig;
    new_node.primitive_topology = util::to_native_topology(primitive_config.topology);

    {
        // Create PSO
        auto const vert_format_native = util::get_native_vertex_format(vertex_format.attributes);
        new_node.raw_pso = create_pipeline_state(*mDevice, root_sig->raw_root_sig, vert_format_native, framebuffer_format, shader_stages, primitive_config);
        util::set_object_name(new_node.raw_pso, "pool graphics pso #%d", int(pool_index));
    }


    return {static_cast<handle::handle_t>(pool_index)};
}

phi::handle::pipeline_state phi::d3d12::PipelineStateObjectPool::createComputePipelineState(phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                                            arg::shader_binary compute_shader,
                                                                                            bool has_root_constants)
{
    root_signature* root_sig;
    uint32_t pool_index;
    // Do things requiring synchronization first
    {
        auto lg = std::lock_guard(mMutex);
        root_sig = mRootSigCache.getOrCreate(*mDevice, shader_arg_shapes, has_root_constants, root_signature_type::compute);
        pool_index = mPool.acquire();
    }

    // Populate new node
    pso_node& new_node = mPool.get(pool_index);
    new_node.associated_root_sig = root_sig;
    new_node.primitive_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    // Create PSO
    new_node.raw_pso = create_compute_pipeline_state(*mDevice, root_sig->raw_root_sig, compute_shader.data, compute_shader.size);
    util::set_object_name(new_node.raw_pso, "pool compute pso #%d", int(pool_index));

    auto const res = handle::pipeline_state{pool_index};
    CC_ASSERT(!isRaytracingPipeline(res));
    return res;
}

phi::handle::pipeline_state phi::d3d12::PipelineStateObjectPool::createRaytracingPipelineState(arg::raytracing_shader_libraries libraries,
                                                                                               arg::raytracing_argument_associations arg_assocs,
                                                                                               arg::raytracing_hit_groups hit_groups,
                                                                                               unsigned max_recursion,
                                                                                               unsigned max_payload_size_bytes,
                                                                                               unsigned max_attribute_size_bytes,
                                                                                               cc::allocator* scratch_alloc)
{
    CC_ASSERT(libraries.size() > 0 && arg_assocs.size() <= limits::max_raytracing_argument_assocs && "zero libraries or too many argument associations");
    CC_ASSERT(hit_groups.size() <= limits::max_raytracing_hit_groups && "too many hit groups");

    unsigned pool_index;
    // Do things requiring synchronization first
    {
        auto lg = std::lock_guard(mMutex);
        pool_index = mPoolRaytracing.acquire();

        rt_pso_node& new_node = mPoolRaytracing.get(pool_index);
        new_node.associated_root_signatures.clear();

        for (auto const& aa : arg_assocs)
        {
            new_node.associated_root_signatures.push_back(
                mRootSigCache.getOrCreate(*mDevice, aa.argument_shapes, aa.has_root_constants, root_signature_type::raytrace_local));
        }
    }

    // Library exports, one per symbol per library
    cc::alloc_vector<D3D12_EXPORT_DESC> export_descs(scratch_alloc);
    export_descs.reserve(libraries.size() * 16);

    cc::alloc_vector<wchar_t const*> all_symbols_contiguous(scratch_alloc);
    all_symbols_contiguous.reserve(export_descs.size());

    // Libraries
    cc::alloc_vector<D3D12_DXIL_LIBRARY_DESC> library_descs(scratch_alloc);
    library_descs.reserve(libraries.size());

    // 128 wchars per string
    // 10 strings per library (wc: 16)
    // 4 strings per hitgroup
    // 10 strings per arg assoc (wc: 16)
    cc::alloc_array<wchar_t> wchar_conv_buf_mem((libraries.size() * 16 + hit_groups.size() * 4 + arg_assocs.size() * 16) * 128, scratch_alloc);
    text_buffer wchar_conv_buf;
    wchar_conv_buf.init(wchar_conv_buf_mem.data(), wchar_conv_buf_mem.size());

    for (auto const& lib : libraries)
    {
        auto& new_desc = library_descs.emplace_back();
        new_desc.DXILLibrary = D3D12_SHADER_BYTECODE{lib.binary.data, lib.binary.size};
        new_desc.NumExports = static_cast<UINT>(lib.exports.size());

        auto const export_desc_offset = export_descs.size();

        for (auto const& exp : lib.exports)
        {
            wchar_t const* const symbol_name = wchar_conv_buf.write_string(exp.entrypoint);

            auto& new_export = export_descs.emplace_back();
            new_export.Name = symbol_name;
            new_export.Flags = D3D12_EXPORT_FLAG_NONE;
            new_export.ExportToRename = nullptr;

            all_symbols_contiguous.push_back(symbol_name);
        }

        new_desc.pExports = export_descs.data() + export_desc_offset;
    }

    // Argument (local root signature) associations
    cc::alloc_vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> rootsig_associations(scratch_alloc);
    rootsig_associations.reserve(arg_assocs.size());

    cc::alloc_vector<wchar_t const*> flat_symbol_names(scratch_alloc);
    flat_symbol_names.reserve(arg_assocs.size() * 16);

    for (auto const& aa : arg_assocs)
    {
        unsigned export_desc_offset = 0;
        for (auto i = 0u; i < aa.library_index; ++i)
        {
            export_desc_offset += library_descs[i].NumExports;
        }

        unsigned flat_symbol_names_offset = unsigned(flat_symbol_names.size());
        for (auto i = 0u; i < aa.export_indices.size(); ++i)
        {
            flat_symbol_names.push_back(export_descs[aa.export_indices[i] + export_desc_offset].Name);
        }

        auto& new_association = rootsig_associations.emplace_back();
        new_association.pSubobjectToAssociate = nullptr; // will be filled in later
        new_association.NumExports = static_cast<UINT>(aa.export_indices.size());
        new_association.pExports = flat_symbol_names.data() + flat_symbol_names_offset;
    }

    // Hit groups
    cc::alloc_vector<D3D12_HIT_GROUP_DESC> hit_group_descs(scratch_alloc);
    hit_group_descs.reserve(hit_groups.size());

    for (auto const& hg : hit_groups)
    {
        D3D12_HIT_GROUP_DESC& new_desc = hit_group_descs.emplace_back();
        new_desc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        new_desc.HitGroupExport = wchar_conv_buf.write_string(hg.name);
        new_desc.ClosestHitShaderImport = wchar_conv_buf.write_string(hg.closest_hit_name);
        new_desc.AnyHitShaderImport = wchar_conv_buf.write_string(hg.any_hit_name);
        new_desc.IntersectionShaderImport = wchar_conv_buf.write_string(hg.intersection_name);
    }

    // shader config + association
    D3D12_RAYTRACING_SHADER_CONFIG shader_config;
    shader_config.MaxPayloadSizeInBytes = max_payload_size_bytes;
    shader_config.MaxAttributeSizeInBytes = max_attribute_size_bytes;

    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shader_config_association;
    shader_config_association.NumExports = static_cast<UINT>(all_symbols_contiguous.size());
    shader_config_association.pExports = all_symbols_contiguous.data();

    // pipeline config
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config;
    pipeline_config.MaxTraceRecursionDepth = max_recursion;

    // global empty root sig
    D3D12_GLOBAL_ROOT_SIGNATURE global_root_sig;
    global_root_sig.pGlobalRootSignature = mEmptyRaytraceRootSignature;

    rt_pso_node& new_node = mPoolRaytracing.get(pool_index);

    cc::alloc_vector<D3D12_STATE_SUBOBJECT> subobjects(scratch_alloc);
    {
        // 1 per library
        // 2 per arg assoc: local root signature, subobject association
        // constant: shader config + association, pipeline config, global empty root sig
        subobjects.reserve(library_descs.size() + arg_assocs.size() * 2 + hit_group_descs.size() + 4);

        for (auto i = 0u; i < library_descs.size(); ++i)
        {
            // subobject for library
            auto& subobj = subobjects.emplace_back();
            subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            subobj.pDesc = &library_descs[i];
        }

        for (auto i = 0u; i < arg_assocs.size(); ++i)
        {
            // subobject for root signature
            auto& subobj_rootsig = subobjects.emplace_back();
            subobj_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
            subobj_rootsig.pDesc = &new_node.associated_root_signatures[i]->raw_root_sig;

            // association
            {
                // fill in subobject pointer
                rootsig_associations[i].pSubobjectToAssociate = &subobj_rootsig;

                // subobject for association
                auto& subobj_rootsig_assoc = subobjects.emplace_back();
                subobj_rootsig_assoc.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
                subobj_rootsig_assoc.pDesc = &rootsig_associations[i];
            }
        }

        for (auto const& hit_desc : hit_group_descs)
        {
            auto& subobj = subobjects.emplace_back();
            subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            subobj.pDesc = &hit_desc;
        }

        // shader config and association
        {
            auto& subobj = subobjects.emplace_back();
            subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
            subobj.pDesc = &shader_config;

            shader_config_association.pSubobjectToAssociate = &subobj;

            auto& subobj_association = subobjects.emplace_back();
            subobj_association.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
            subobj_association.pDesc = &shader_config_association;
        }

        // pipeline config
        {
            auto& subobj = subobjects.emplace_back();
            subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
            subobj.pDesc = &pipeline_config;
        }

        // empty global root sig
        {
            auto& subobj = subobjects.emplace_back();
            subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
            subobj.pDesc = &global_root_sig;
        }
    }

    D3D12_STATE_OBJECT_DESC state_obj = {};
    state_obj.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    state_obj.NumSubobjects = static_cast<UINT>(subobjects.size());
    state_obj.pSubobjects = subobjects.data();

    // Create the state object
    PHI_D3D12_VERIFY(mDevice->CreateStateObject(&state_obj, IID_PPV_ARGS(&new_node.raw_state_object)));
    // QI the properties for access to shader identifiers
    PHI_D3D12_VERIFY(new_node.raw_state_object->QueryInterface(IID_PPV_ARGS(&new_node.raw_state_object_props)));

    return {pool_index | gc_d3d12_is_raytracing_pso_bit};
}

void phi::d3d12::PipelineStateObjectPool::free(phi::handle::pipeline_state ps)
{
    if (!ps.is_valid())
        return;

    if (isRaytracingPipeline(ps))
    {
        rt_pso_node& freed_node = mPoolRaytracing.get(ps._value);
        freed_node.raw_state_object->Release();
        freed_node.raw_state_object_props->Release();

        {
            auto lg = std::lock_guard(mMutex);
            mPoolRaytracing.release(ps._value);
        }
    }
    else
    {
        // This requires no synchronization, as D3D12MA internally syncs
        pso_node& freed_node = mPool.get(ps._value);
        freed_node.raw_pso->Release();

        {
            // This is a write access to the pool and must be synced
            auto lg = std::lock_guard(mMutex);
            mPool.release(ps._value);
        }
    }
}

void phi::d3d12::PipelineStateObjectPool::initialize(ID3D12Device5* device_rt, unsigned max_num_psos, unsigned max_num_psos_raytracing, cc::allocator* static_alloc)
{
    // Component init
    mDevice = device_rt;
    mStaticAllocator = static_alloc;
    mPool.initialize(max_num_psos, static_alloc);
    mPoolRaytracing.initialize(max_num_psos_raytracing, static_alloc);
    mRootSigCache.initialize((max_num_psos / 2) + max_num_psos_raytracing, static_alloc); // almost arbitrary, revisit if this blows up

    // Create empty raytracing rootsig
    mEmptyRaytraceRootSignature = mRootSigCache.getOrCreate(*mDevice, {}, false, root_signature_type::raytrace_global)->raw_root_sig;

    // Create global (indirect drawing) command signatures for draw and indexed draw
    static_assert(sizeof(D3D12_DRAW_ARGUMENTS) == sizeof(gpu_indirect_command_draw), "gpu argument type compiles to incorrect size");
    static_assert(sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) == sizeof(gpu_indirect_command_draw_indexed), "gpu argument type compiles to incorrect size");

    D3D12_INDIRECT_ARGUMENT_DESC indirect_arg;
    indirect_arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &indirect_arg;
    desc.ByteStride = sizeof(gpu_indirect_command_draw);
    desc.NodeMask = 0;
    mDevice->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&mGlobalComSigDraw));

    indirect_arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    desc.ByteStride = sizeof(gpu_indirect_command_draw_indexed);
    mDevice->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&mGlobalComSigDrawIndexed));
}

void phi::d3d12::PipelineStateObjectPool::destroy()
{
    auto num_leaks = 0;
    mPool.iterate_allocated_nodes([&](pso_node& leaked_node) {
        ++num_leaks;
        leaked_node.raw_pso->Release();
    });

    mPoolRaytracing.iterate_allocated_nodes([&](rt_pso_node& leaked_node) {
        ++num_leaks;
        leaked_node.raw_state_object->Release();
        leaked_node.raw_state_object_props->Release();
    });

    if (num_leaks > 0)
    {
        PHI_LOG("leaked {} handle::pipeline_state object{}", num_leaks, (num_leaks == 1 ? "" : "s"));
    }

    mRootSigCache.destroy();

    mGlobalComSigDraw->Release();
    mGlobalComSigDrawIndexed->Release();
}

const phi::d3d12::PipelineStateObjectPool::rt_pso_node& phi::d3d12::PipelineStateObjectPool::getRaytrace(phi::handle::pipeline_state ps) const
{
    return mPoolRaytracing.get(ps._value);
}

bool phi::d3d12::PipelineStateObjectPool::isRaytracingPipeline(phi::handle::pipeline_state ps) const
{
    return (ps._value & gc_d3d12_is_raytracing_pso_bit) != 0;
}
