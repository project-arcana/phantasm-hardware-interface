#include "pso_pool.hh"

#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/detail/byte_util.hh>

#include <phantasm-hardware-interface/d3d12/common/log.hh>
#include <phantasm-hardware-interface/d3d12/common/native_enum.hh>
#include <phantasm-hardware-interface/d3d12/common/util.hh>
#include <phantasm-hardware-interface/d3d12/common/verify.hh>
#include <phantasm-hardware-interface/d3d12/pipeline_state.hh>

#include "resource_pool.hh"

namespace
{
constexpr phi::handle::index_t gc_raytracing_handle_offset = 1073741824;
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

    {
        // Create PSO
        auto const vert_format_native = util::get_native_vertex_format(vertex_format.attributes);
        new_node.raw_pso = create_pipeline_state(*mDevice, root_sig->raw_root_sig, vert_format_native, framebuffer_format, shader_stages, primitive_config);
        util::set_object_name(new_node.raw_pso, "pool pso #%d", int(pool_index));
    }

    new_node.primitive_topology = util::to_native_topology(primitive_config.topology);

    return {static_cast<handle::index_t>(pool_index)};
}

phi::handle::pipeline_state phi::d3d12::PipelineStateObjectPool::createComputePipelineState(phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                                            arg::shader_binary compute_shader,
                                                                                            bool has_root_constants)
{
    root_signature* root_sig;
    unsigned pool_index;
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

    return {static_cast<handle::index_t>(pool_index)};
}

phi::handle::pipeline_state phi::d3d12::PipelineStateObjectPool::createRaytracingPipelineState(arg::raytracing_shader_libraries libraries,
                                                                                               arg::raytracing_argument_associations arg_assocs,
                                                                                               arg::raytracing_hit_groups hit_groups,
                                                                                               unsigned max_recursion,
                                                                                               unsigned max_payload_size_bytes,
                                                                                               unsigned max_attribute_size_bytes)
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
    cc::vector<D3D12_EXPORT_DESC> export_descs;
    export_descs.reserve(libraries.size() * 10);

    cc::vector<wchar_t const*> all_symbols_contiguous;
    all_symbols_contiguous.reserve(export_descs.size());

    // Libraries
    cc::vector<D3D12_DXIL_LIBRARY_DESC> library_descs;
    {
        library_descs.reserve(libraries.size());

        for (auto const& lib : libraries)
        {
            auto& new_desc = library_descs.emplace_back();
            new_desc.DXILLibrary = D3D12_SHADER_BYTECODE{lib.binary.data, lib.binary.size};
            new_desc.NumExports = static_cast<UINT>(lib.symbols.size());

            auto const export_desc_offset = export_descs.size();

            for (wchar_t const* const symbol_string : lib.symbols)
            {
                auto& new_export = export_descs.emplace_back();
                new_export.Name = symbol_string;
                new_export.Flags = D3D12_EXPORT_FLAG_NONE;
                new_export.ExportToRename = nullptr;

                all_symbols_contiguous.push_back(symbol_string);
            }

            new_desc.pExports = export_descs.data() + export_desc_offset;
        }
    }

    // Argument (local root signature) associations
    cc::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> rootsig_associations;
    {
        rootsig_associations.reserve(arg_assocs.size());

        for (auto const& aa : arg_assocs)
        {
            auto& new_association = rootsig_associations.emplace_back();
            new_association.pSubobjectToAssociate = nullptr; // will be filled in later
            new_association.NumExports = static_cast<UINT>(aa.symbols.size());
            new_association.pExports = const_cast<wchar_t const**>(aa.symbols.data());
        }
    }

    // Hit groups
    cc::vector<D3D12_HIT_GROUP_DESC> hit_group_descs;
    hit_group_descs.reserve(hit_groups.size());

    for (auto const& hg : hit_groups)
    {
        D3D12_HIT_GROUP_DESC& new_desc = hit_group_descs.emplace_back();
        new_desc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        new_desc.HitGroupExport = hg.name;
        new_desc.ClosestHitShaderImport = hg.closest_hit_symbol;
        new_desc.AnyHitShaderImport = hg.any_hit_symbol;
        new_desc.IntersectionShaderImport = hg.intersection_symbol;
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

    cc::vector<D3D12_STATE_SUBOBJECT> subobjects;
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

    return {static_cast<handle::index_t>(pool_index + gc_raytracing_handle_offset)};
}

void phi::d3d12::PipelineStateObjectPool::free(phi::handle::pipeline_state ps)
{
    // TODO: dangle check
    if (!ps.is_valid())
        return;

    if (isRaytracingPipeline(ps))
    {
        unsigned const pool_idx = static_cast<unsigned>(ps.index - gc_raytracing_handle_offset);
        rt_pso_node& freed_node = mPoolRaytracing.get(pool_idx);
        freed_node.raw_state_object->Release();
        freed_node.raw_state_object_props->Release();

        {
            auto lg = std::lock_guard(mMutex);
            mPoolRaytracing.release(pool_idx);
        }
    }
    else
    {
        // This requires no synchronization, as D3D12MA internally syncs
        unsigned const pool_idx = static_cast<unsigned>(ps.index);
        pso_node& freed_node = mPool.get(pool_idx);
        freed_node.raw_pso->Release();

        {
            // This is a write access to the pool and must be synced
            auto lg = std::lock_guard(mMutex);
            mPool.release(pool_idx);
        }
    }
}

void phi::d3d12::PipelineStateObjectPool::initialize(ID3D12Device5* device_rt, unsigned max_num_psos, unsigned max_num_psos_raytracing)
{
    CC_ASSERT(max_num_psos < gc_raytracing_handle_offset && "unsupported amount of PSOs");
    CC_ASSERT(max_num_psos_raytracing < gc_raytracing_handle_offset && "unsupported amount of raytracing PSOs");

    mDevice = device_rt;
    mPool.initialize(max_num_psos);
    mPoolRaytracing.initialize(max_num_psos_raytracing);
    mRootSigCache.initialize((max_num_psos / 2) + max_num_psos_raytracing); // almost arbitrary, revisit if this blows up

    mEmptyRaytraceRootSignature = mRootSigCache.getOrCreate(*mDevice, {}, false, root_signature_type::raytrace_global)->raw_root_sig;
}

void phi::d3d12::PipelineStateObjectPool::destroy()
{
    auto num_leaks = 0;
    mPool.iterate_allocated_nodes([&](pso_node& leaked_node, unsigned) {
        ++num_leaks;
        leaked_node.raw_pso->Release();
    });

    mPoolRaytracing.iterate_allocated_nodes([&](rt_pso_node& leaked_node, unsigned) {
        ++num_leaks;
        leaked_node.raw_state_object->Release();
        leaked_node.raw_state_object_props->Release();
    });

    if (num_leaks > 0)
    {
        log::info()("leaked {} handle::pipeline_state object{}", num_leaks, (num_leaks == 1 ? "" : "s"));
    }

    mRootSigCache.destroy();
}

const phi::d3d12::PipelineStateObjectPool::rt_pso_node& phi::d3d12::PipelineStateObjectPool::getRaytrace(phi::handle::pipeline_state ps) const
{
    return mPoolRaytracing.get(static_cast<unsigned>(ps.index - gc_raytracing_handle_offset));
}

bool phi::d3d12::PipelineStateObjectPool::isRaytracingPipeline(phi::handle::pipeline_state ps) const
{
    return ps.index >= gc_raytracing_handle_offset;
}
