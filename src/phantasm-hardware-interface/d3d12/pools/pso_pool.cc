#include "pso_pool.hh"

#include <clean-core/alloc_array.hh>
#include <clean-core/alloc_vector.hh>
#include <clean-core/utility.hh>

#include <clean-core/native/wchar_conversion.hh>

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/log.hh>

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
} // namespace

phi::handle::pipeline_state phi::d3d12::PipelineStateObjectPool::createPipelineState(phi::arg::vertex_format vertex_format,
                                                                                     phi::arg::framebuffer_config const& framebuffer_format,
                                                                                     phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                                     bool has_root_constants,
                                                                                     phi::arg::graphics_shaders shader_stages,
                                                                                     phi::pipeline_config const& primitive_config,
                                                                                     char const* dbg_name)
{
    root_signature* pRootSig = nullptr;
    ID3D12CommandSignature* pDrawIDComSig = nullptr;

    bool const bEnableDrawID = primitive_config.allow_draw_indirect_with_id;

    if (bEnableDrawID && !has_root_constants)
    {
        PHI_LOG_ERROR("Indirect Draw ID mode requires enabled root constants. Aborting compilation of PSO with debug name: {}",
                      dbg_name ? dbg_name : "unnamed (nullptr)");
        return handle::null_pipeline_state;
    }

    // Do things requiring synchronization first
    {
        auto lg = std::lock_guard(mMutex);

        pRootSig = mRootSigCache.getOrCreate(*mDevice, shader_arg_shapes, has_root_constants, root_signature_type::graphics);

        if (bEnableDrawID)
        {
            pDrawIDComSig = mComSigCache.getOrCreateDrawIDComSig(mDevice, pRootSig);
        }
    }

    if (!pRootSig)
    {
        PHI_LOG_ERROR("Failed to create root signature when compiling PSO, debug name: {}", dbg_name ? dbg_name : "unnamed (nullptr)");
        return phi::handle::null_pipeline_state;
    }

    if (bEnableDrawID && !pDrawIDComSig)
    {
        PHI_LOG_ERROR("Failed to create Draw ID command signature when compiling PSO, debug name: {}", dbg_name ? dbg_name : "unnamed (nullptr)");
        return phi::handle::null_pipeline_state;
    }

    auto const vertexFormatNative = util::get_native_vertex_format(vertex_format.attributes);
    ID3D12PipelineState* const pPipelineState
        = create_pipeline_state(*mDevice, pRootSig->raw_root_sig, vertexFormatNative, framebuffer_format, shader_stages, primitive_config);

    if (!pPipelineState)
    {
        PHI_LOG_ERROR("Failed to compile PSO, debug name: {}", dbg_name ? dbg_name : "unnamed (nullptr)");
        return phi::handle::null_pipeline_state;
    }

    util::set_object_name(pPipelineState, "%s", dbg_name ? dbg_name : "Unnamed Graphics PSO");

    uint32_t const res = mPool.acquire();

    // Populate new node
    pso_node& new_node = mPool.get(res);
    new_node.pPSO = pPipelineState;
    new_node.pAssociatedRootSig = pRootSig;
    new_node.pAssociatedComSigForDrawID = pDrawIDComSig;
    new_node.primitive_topology = util::to_native_topology(primitive_config.topology);

    return {res};
}

phi::handle::pipeline_state phi::d3d12::PipelineStateObjectPool::createComputePipelineState(phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                                            arg::shader_binary compute_shader,
                                                                                            bool has_root_constants,
                                                                                            char const* dbg_name)
{
    root_signature* pRootSig = nullptr;
    // Do things requiring synchronization first
    {
        auto lg = std::lock_guard(mMutex);
        pRootSig = mRootSigCache.getOrCreate(*mDevice, shader_arg_shapes, has_root_constants, root_signature_type::compute);
    }

    if (!pRootSig)
    {
        PHI_LOG_ERROR("Failed to create root signature when compiling PSO, debug name: {}", dbg_name ? dbg_name : "unnamed (nullptr)");
        return phi::handle::null_pipeline_state;
    }

    ID3D12PipelineState* const pPipelineState = create_compute_pipeline_state(*mDevice, pRootSig->raw_root_sig, compute_shader.data, compute_shader.size);

    if (!pPipelineState)
    {
        PHI_LOG_ERROR("Failed to compile PSO, debug name: {}", dbg_name ? dbg_name : "unnamed (nullptr)");
        return phi::handle::null_pipeline_state;
    }

    util::set_object_name(pPipelineState, "%s", dbg_name ? dbg_name : "Unnamed Compute PSO");

    uint32_t const res = mPool.acquire();

    // Populate new node
    pso_node& new_node = mPool.get(res);
    new_node.pPSO = pPipelineState;
    new_node.pAssociatedRootSig = pRootSig;
    new_node.pAssociatedComSigForDrawID = nullptr;
    new_node.primitive_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    return {res};
}

phi::handle::pipeline_state phi::d3d12::PipelineStateObjectPool::createRaytracingPipelineState(cc::span<const arg::raytracing_shader_library> libraries,
                                                                                               cc::span<const arg::raytracing_argument_association> arg_assocs,
                                                                                               cc::span<const arg::raytracing_hit_group> hit_groups,
                                                                                               unsigned max_recursion,
                                                                                               unsigned max_payload_size_bytes,
                                                                                               unsigned max_attribute_size_bytes,
                                                                                               cc::allocator* scratch_alloc,
                                                                                               char const* dbg_name)
{
    CC_ASSERT(libraries.size() > 0 && arg_assocs.size() <= limits::max_raytracing_argument_assocs && "zero libraries or too many argument associations");
    CC_ASSERT(hit_groups.size() <= limits::max_raytracing_hit_groups && "too many hit groups");

    unsigned const pool_index = mPoolRaytracing.acquire();
    rt_pso_node& new_node = mPoolRaytracing.get(pool_index);
    new_node.associated_root_signatures.clear();

    // Do things requiring synchronization first
    {
        // accesses to root sig cache require sync
        auto lg = std::lock_guard(mMutex);

        // each argument association constitutes a local root signature
        // (the global root signature is empty and shared across all RT PSOs)
        for (auto const& aa : arg_assocs)
        {
            root_signature* const pLocalRootSig
                = mRootSigCache.getOrCreate(*mDevice, aa.argument_shapes, aa.has_root_constants, root_signature_type::raytrace_local);
            CC_ASSERT(pLocalRootSig != nullptr && "Failed to create local root siganture for raytracing PSO");

            new_node.associated_root_signatures.push_back(pLocalRootSig);
        }
    }

    unsigned const num_expected_exports = unsigned(libraries.size()) * 16u;

    // Library exports, one per symbol per library
    cc::alloc_vector<D3D12_EXPORT_DESC> export_descs(scratch_alloc);
    export_descs.reserve(num_expected_exports);

    // parallel array to export_descs, contiguous string pointers
    cc::alloc_vector<wchar_t const*> par_export_symbols(scratch_alloc);
    par_export_symbols.reserve(num_expected_exports);

    // parallel array to export_descs, stage per element
    cc::alloc_vector<shader_stage> par_export_stages(scratch_alloc);
    par_export_stages.reserve(num_expected_exports);

    struct export_auxilliary_info
    {
        int linear_index = -1;  // index into export_descs and it's parallel arrays
        int rootsig_index = -1; // index into arg_assocs and it's parallel arrays
    };

    cc::alloc_vector<export_auxilliary_info> identifiable_exports(scratch_alloc);
    identifiable_exports.reserve(num_expected_exports / 2); // most exports are usually part of hitgroups

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
        new_desc.NumExports = static_cast<UINT>(lib.shader_exports.size());

        auto const export_desc_offset = export_descs.size();

        for (auto const& exp : lib.shader_exports)
        {
            wchar_t const* const symbol_name = wchar_conv_buf.write_string(exp.entrypoint);

            int const new_export_index = int(export_descs.size());

            // create export description
            auto& new_export = export_descs.emplace_back();
            new_export.Name = symbol_name;
            new_export.Flags = D3D12_EXPORT_FLAG_NONE;
            new_export.ExportToRename = nullptr;

            // create elements in parallel arrays
            par_export_symbols.push_back(symbol_name);
            par_export_stages.push_back(exp.stage);

            if (shader_stage_mask_ray_identifiable & exp.stage)
            {
                // if this stage is "identifiable" (raygen, miss or callable), add its index to the list
                identifiable_exports.push_back({new_export_index});
            }
            else
            {
                CC_ASSERT(shader_stage_mask_ray_hitgroup & exp.stage && "unexpected stage");
            }
        }

        new_desc.pExports = export_descs.data() + export_desc_offset;
    }

    // Hit groups
    cc::alloc_vector<D3D12_HIT_GROUP_DESC> hit_group_descs(scratch_alloc);
    hit_group_descs.reserve(hit_groups.size());

    cc::alloc_vector<int> hit_group_rootsig_indices(scratch_alloc);
    hit_group_rootsig_indices.resize(hit_groups.size(), -1);

    auto f_get_export_name_or_nullptr = [&](int index, shader_stage stage_verification) -> wchar_t const*
    {
        if (index < 0)
            return nullptr;

        CC_ASSERT(index < int(export_descs.size()) && "hitgroup shader index OOB");
        CC_ASSERT(par_export_stages[index] == stage_verification && "hitgroup shader index targets the wrong stage");
        return export_descs[index].Name;
    };

    for (auto const& hg : hit_groups)
    {
        D3D12_HIT_GROUP_DESC& new_desc = hit_group_descs.emplace_back();
        new_desc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES; // no support for procedural hitgroups yet
        new_desc.HitGroupExport = wchar_conv_buf.write_string(hg.name);
        new_desc.ClosestHitShaderImport = f_get_export_name_or_nullptr(hg.closest_hit_export_index, shader_stage::ray_closest_hit);
        new_desc.AnyHitShaderImport = f_get_export_name_or_nullptr(hg.any_hit_export_index, shader_stage::ray_any_hit);
        new_desc.IntersectionShaderImport = f_get_export_name_or_nullptr(hg.intersection_export_index, shader_stage::ray_intersect);

        CC_ASSERT(new_desc.ClosestHitShaderImport != nullptr && "missing required closest hit shader entry");
    }

    // Argument (local root signature) associations
    cc::alloc_vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> rootsig_associations(scratch_alloc);
    rootsig_associations.reserve(arg_assocs.size());

    // symbol names, partitioned and ordered according to the given arg associations
    // D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION::pExports points into multiple sections in this buffer
    cc::alloc_vector<wchar_t const*> rootsig_symbol_name_buffer(scratch_alloc);
    rootsig_symbol_name_buffer.reserve(arg_assocs.size() * 16);

    for (auto aa_i = 0u; aa_i < arg_assocs.size(); ++aa_i)
    {
        arg::raytracing_argument_association const& aa = arg_assocs[aa_i];

        unsigned const flat_symbol_names_start_offset = unsigned(rootsig_symbol_name_buffer.size());
        unsigned num_exports = 0;

        if (aa.target_type == arg::raytracing_argument_association::e_target_identifiable_shader)
        {
            // aa::target_indices are indexing into identifiable_exports

            for (uint32_t identifiable_i : aa.target_indices)
            {
                auto& identfiable_info = identifiable_exports[identifiable_i];

                // write rootsig index
                identfiable_info.rootsig_index = int(aa_i);
                rootsig_symbol_name_buffer.push_back(export_descs[identfiable_info.linear_index].Name);
            }

            num_exports = UINT(aa.target_indices.size());
        }
        else /* arg::raytracing_argument_association::e_target_hitgroup */
        {
            // aa::target_indices are indexing into hit_group_descs

            for (uint32_t hitgroup_i : aa.target_indices)
            {
                auto const& hg_desc = hit_group_descs[hitgroup_i];

                // write rootsig index
                hit_group_rootsig_indices[hitgroup_i] = int(aa_i);

                rootsig_symbol_name_buffer.push_back(hg_desc.ClosestHitShaderImport);
                ++num_exports;

                if (hg_desc.AnyHitShaderImport)
                {
                    rootsig_symbol_name_buffer.push_back(hg_desc.AnyHitShaderImport);
                    ++num_exports;
                }

                if (hg_desc.IntersectionShaderImport)
                {
                    rootsig_symbol_name_buffer.push_back(hg_desc.IntersectionShaderImport);
                    ++num_exports;
                }
            }
        }


        auto& new_association = rootsig_associations.emplace_back();
        new_association.pSubobjectToAssociate = nullptr; // will be filled in later
        new_association.NumExports = num_exports;
        new_association.pExports = rootsig_symbol_name_buffer.data() + flat_symbol_names_start_offset;
    }

    // shader config + association
    D3D12_RAYTRACING_SHADER_CONFIG shader_config = {};
    shader_config.MaxPayloadSizeInBytes = max_payload_size_bytes;
    shader_config.MaxAttributeSizeInBytes = max_attribute_size_bytes;

    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shader_config_association = {};
    shader_config_association.NumExports = UINT(par_export_symbols.size());
    shader_config_association.pExports = par_export_symbols.data();

    // pipeline config
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = {};
    pipeline_config.MaxTraceRecursionDepth = max_recursion;

    // global empty root sig
    D3D12_GLOBAL_ROOT_SIGNATURE global_root_sig = {};
    global_root_sig.pGlobalRootSignature = mEmptyRaytraceRootSignature;

    cc::alloc_vector<D3D12_STATE_SUBOBJECT> subobjects(scratch_alloc);
    {
        // create "subobjects" which finally compose into the PSO

        // 1 per shader library
        // 2 per argument association: local rootsig and subobject association
        // always: shader config + association, pipeline config, global empty rootsig
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
            // subobject for local root signature
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

        // empty global rootsig
        {
            auto& subobj = subobjects.emplace_back();
            subobj.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
            subobj.pDesc = &global_root_sig;
        }
    }

    D3D12_STATE_OBJECT_DESC state_obj = {};
    state_obj.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    state_obj.NumSubobjects = UINT(subobjects.size());
    state_obj.pSubobjects = subobjects.data();

    // Create the state object
    PHI_D3D12_VERIFY(mDevice->CreateStateObject(&state_obj, IID_PPV_ARGS(&new_node.raw_state_object)));
    // QI the properties for access to shader identifiers
    PHI_D3D12_VERIFY(new_node.raw_state_object->QueryInterface(IID_PPV_ARGS(&new_node.raw_state_object_props)));

    {
        // cache shader identifiers for all exports and hitgroups

        new_node.identifiable_shader_infos = cc::alloc_array<rt_pso_node::export_info>::uninitialized(identifiable_exports.size(), mDynamicAllocator);
        new_node.hitgroup_infos = cc::alloc_array<rt_pso_node::export_info>::uninitialized(hit_group_descs.size(), mDynamicAllocator);

        for (auto i = 0u; i < identifiable_exports.size(); ++i)
        {
            auto const& identifiable_info = identifiable_exports[i];
            rt_pso_node::export_info& export_info = new_node.identifiable_shader_infos[i];


            // write shader identifier
            {
                void const* const export_identifier = new_node.raw_state_object_props->GetShaderIdentifier(par_export_symbols[identifiable_info.linear_index]);
                CC_ASSERT(export_identifier != nullptr && "cannot find exported symbol in library");
                std::memcpy(export_info.shader_identifier, export_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            }

            // write rootsig info
            {
                if (identifiable_info.rootsig_index == -1)
                {
                    // no associated rootsig - this is valid but rarely intentional
                    // tools like PIX will warn as if it were an error, however it's not strictly wrong
                    //
                    // Austin Kinross (MS):
                    //      It's valid for a shader to not be associated with any local root signature.
                    //      This is often unintended though (and therefore a bug), which is why PIX calls attention to it.

                    PHI_LOG_WARN("createRayTracingPipelineState: identifiable shader #{} (\"{}\") has no argument association. this is valid but "
                                 "possibly unintended",
                                 i, par_export_symbols[identifiable_info.linear_index]);

                    export_info.arg_info.initialize_no_rootsig();
                }
                else
                {
                    auto const& arg_assoc = arg_assocs[identifiable_info.rootsig_index];
                    export_info.arg_info.initialize(arg_assoc.argument_shapes, arg_assoc.has_root_constants);
                }
            }
        }

        for (auto i = 0u; i < hit_group_descs.size(); ++i)
        {
            rt_pso_node::export_info& export_info = new_node.hitgroup_infos[i];
            // auto const export_i = hitgroup_exports[i].linear_index;

            // write hitgroup identifier
            {
                void const* const hitgroup_identifier = new_node.raw_state_object_props->GetShaderIdentifier(hit_group_descs[i].HitGroupExport);
                CC_ASSERT(hitgroup_identifier != nullptr && "cannot find hitgroup symbol in library");
                std::memcpy(export_info.shader_identifier, hitgroup_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            }

            // write rootsig info
            {
                int rootsig_i = hit_group_rootsig_indices[i];

                if (rootsig_i == -1)
                {
                    // no associated rootsig - see above

                    PHI_LOG_WARN("createRayTracingPipelineState: hitgroup #{} (\"{}\") has no argument association. this is valid but "
                                 "possibly unintended",
                                 i, hit_groups[i].name);

                    export_info.arg_info.initialize_no_rootsig();
                }
                else
                {
                    auto const& arg_assoc = arg_assocs[rootsig_i];
                    export_info.arg_info.initialize(arg_assoc.argument_shapes, arg_assoc.has_root_constants);
                }
            }
        }
    }

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

        mPoolRaytracing.release(ps._value);
    }
    else
    {
        // This requires no synchronization, as D3D12MA internally syncs
        pso_node& freed_node = mPool.get(ps._value);
        freed_node.pPSO->Release();

        mPool.release(ps._value);
    }
}

void phi::d3d12::PipelineStateObjectPool::initialize(
    ID3D12Device5* device_rt, unsigned max_num_psos, unsigned max_num_psos_raytracing, cc::allocator* static_alloc, cc::allocator* dynamic_alloc)
{
    // Component init
    mDevice = device_rt;
    mDynamicAllocator = dynamic_alloc;
    mPool.initialize(max_num_psos, static_alloc);
    mPoolRaytracing.initialize(max_num_psos_raytracing, static_alloc);

    mRootSigCache.initialize((max_num_psos / 2) + max_num_psos_raytracing, static_alloc); // almost arbitrary, revisit if this blows up
    mComSigCache.initialize((max_num_psos / 2), static_alloc);

    // Create empty raytracing rootsig
    mEmptyRaytraceRootSignature = mRootSigCache.getOrCreate(*mDevice, {}, false, root_signature_type::raytrace_global)->raw_root_sig;

    // Create global (indirect drawing) command signatures
    mGlobalComSigDraw = createCommandSignatureForDraw(mDevice);
    mGlobalComSigDrawIndexed = createCommandSignatureForDrawIndexed(mDevice);
    mGlobalComSigDispatch = createCommandSignatureForDispatch(mDevice);
}

void phi::d3d12::PipelineStateObjectPool::destroy()
{
    auto num_leaks = 0;
    mPool.iterate_allocated_nodes(
        [&](pso_node& leaked_node)
        {
            ++num_leaks;
            leaked_node.pPSO->Release();
        });

    mPoolRaytracing.iterate_allocated_nodes(
        [&](rt_pso_node& leaked_node)
        {
            ++num_leaks;
            leaked_node.raw_state_object->Release();
            leaked_node.raw_state_object_props->Release();
        });

    if (num_leaks > 0)
    {
        PHI_LOG("leaked {} handle::pipeline_state object{}", num_leaks, (num_leaks == 1 ? "" : "s"));
    }

    mRootSigCache.destroy();
    mComSigCache.destroy();

    mGlobalComSigDraw->Release();
    mGlobalComSigDrawIndexed->Release();
    mGlobalComSigDispatch->Release();
}

const phi::d3d12::PipelineStateObjectPool::rt_pso_node& phi::d3d12::PipelineStateObjectPool::getRaytrace(phi::handle::pipeline_state ps) const
{
    return mPoolRaytracing.get(ps._value);
}

bool phi::d3d12::PipelineStateObjectPool::isRaytracingPipeline(phi::handle::pipeline_state ps) const
{
    return (ps._value & gc_d3d12_is_raytracing_pso_bit) != 0;
}

void phi::d3d12::PipelineStateObjectPool::pso_argument_info::initialize(phi::arg::shader_arg_shapes shapes, bool root_consts_present)
{
    static_assert(limits::max_shader_arguments * eb_arg_stride < sizeof(flag_t) * 8 - 2, "flags must be larger for this amount of shader arguments");

    flags = 0;

    if (root_consts_present)
        cc::set_bit(flags, eb_has_root_constants);

    for (auto i = 0u; i < shapes.size(); ++i)
    {
        auto const& shape = shapes[i];

        if (shape.has_cbv)
            cc::set_bit(flags, eb_arg_stride * i + eb_has_cbv);

        if (shape.num_srvs + shape.num_uavs > 0)
            cc::set_bit(flags, eb_arg_stride * i + eb_has_srv_uav);

        if (shape.num_samplers > 0)
            cc::set_bit(flags, eb_arg_stride * i + eb_has_sampler);
    }
}

void phi::d3d12::PipelineStateObjectPool::pso_argument_info::initialize_no_rootsig()
{
    flags = 0;
    cc::set_bit(flags, eb_no_rootsig_available);
}

bool phi::d3d12::PipelineStateObjectPool::pso_argument_info::is_matching_inputs(phi::arg::shader_arg_shapes shapes, unsigned root_constant_bytes) const
{
    if (has_no_rootsig())
    {
        bool are_params_empty = shapes.empty() && root_constant_bytes == 0;

        if (!are_params_empty)
            PHI_LOG_ERROR("shader table write invalid - attempted to write parameters to shader/hitgroup that does not take any");

        return are_params_empty;
    }
    else
    {
        bool is_rootconst_matching = has_root_consts() == (root_constant_bytes > 0);

        if (!is_rootconst_matching)
        {
            if (root_constant_bytes > 0)
                PHI_LOG_ERROR("shader table write invalid - attempted to write {} bytes of root constants to shader/hitgroup that does not take any",
                              root_constant_bytes);
            else
                PHI_LOG_ERROR("shader table write invalid - omitted root constant write to shader/hitgroup that requires them");
        }

        bool are_descriptors_matching = true;
        for (auto i = 0u; i < shapes.size(); ++i)
        {
            auto const& shape = shapes[i];

            bool match_cbv = shape.has_cbv == has_cbv(i);
            bool match_srv_uav = (shape.num_srvs + shape.num_uavs > 0) == has_srv_uav(i);
            bool match_sampler = (shape.num_samplers > 0) == has_sampler(i);

            if (!match_cbv)
                PHI_LOG_ERROR("shader table write invalid - argument #{} - CBV required: {} vs supplied: {}", i, has_cbv(i), shape.has_cbv);

            if (!match_srv_uav)
                PHI_LOG_ERROR("shader table write invalid - argument #{} - SRV/UAVs required: {} vs supplied: {} / {}", i, has_srv_uav(i),
                              shape.num_srvs, shape.num_uavs);

            if (!match_sampler)
                PHI_LOG_ERROR("shader table write invalid - argument #{} - Samplers required: {} vs supplied: {}", has_sampler(i), shape.num_samplers);

            are_descriptors_matching = are_descriptors_matching && match_cbv && match_srv_uav && match_sampler;
        }

        // NOTE: if the rootsig still has entries beyond the written arguments, it's not necessarily a mistake
        // it might either be a partial update, or those descriptors/VAs are not accessed in the shader dispatch

        return is_rootconst_matching && are_descriptors_matching;
    }
}
