#pragma once

#include <mutex>

#include <clean-core/alloc_array.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/common/container/linked_pool.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>

#include "root_sig_cache.hh"

namespace phi::d3d12
{
class ResourcePool;

/// The high-level allocator for PSOs and root signatures
/// Synchronized
class PipelineStateObjectPool
{
public:
    // frontend-facing API

    [[nodiscard]] handle::pipeline_state createPipelineState(arg::vertex_format vertex_format,
                                                             const arg::framebuffer_config& framebuffer_format,
                                                             arg::shader_arg_shapes shader_arg_shapes,
                                                             bool has_root_constants,
                                                             arg::graphics_shaders shader_stages,
                                                             phi::pipeline_config const& primitive_config);

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::shader_arg_shapes shader_arg_shapes, arg::shader_binary compute_shader, bool has_root_constants);

    [[nodiscard]] handle::pipeline_state createRaytracingPipelineState(arg::raytracing_shader_libraries libraries,
                                                                       arg::raytracing_argument_associations arg_assocs,
                                                                       arg::raytracing_hit_groups hit_groups,
                                                                       unsigned max_recursion,
                                                                       unsigned max_payload_size_bytes,
                                                                       unsigned max_attribute_size_bytes,
                                                                       cc::allocator* scratch_alloc);

    void free(handle::pipeline_state ps);

public:
    struct pso_node
    {
        ID3D12PipelineState* raw_pso;
        root_signature* associated_root_sig;
        D3D12_PRIMITIVE_TOPOLOGY primitive_topology;
    };

    struct rt_pso_node
    {
        ID3D12StateObject* raw_state_object;
        ID3D12StateObjectProperties* raw_state_object_props;
        cc::capped_vector<root_signature*, limits::max_raytracing_argument_assocs> associated_root_signatures;

        struct export_info
        {
            std::byte shader_identifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
        };

        cc::alloc_array<export_info> export_infos;
        cc::alloc_array<export_info> hitgroup_infos;
    };

public:
    // internal API

    void initialize(ID3D12Device5* device_rt, unsigned max_num_psos, unsigned max_num_psos_raytracing, cc::allocator* static_alloc, cc::allocator* dynamic_alloc);
    void destroy();

    [[nodiscard]] pso_node const& get(handle::pipeline_state ps) const { return mPool.get(ps._value); }

    [[nodiscard]] rt_pso_node const& getRaytrace(handle::pipeline_state ps) const;

    bool isRaytracingPipeline(handle::pipeline_state ps) const;

    ID3D12CommandSignature* getGlobalComSigDraw() const { return mGlobalComSigDraw; }
    ID3D12CommandSignature* getGlobalComSigDrawIndexed() const { return mGlobalComSigDrawIndexed; }

private:
    ID3D12Device5* mDevice = nullptr;
    cc::allocator* mDynamicAllocator = nullptr;

    RootSignatureCache mRootSigCache;
    ID3D12RootSignature* mEmptyRaytraceRootSignature = nullptr;
    ID3D12CommandSignature* mGlobalComSigDraw = nullptr;
    ID3D12CommandSignature* mGlobalComSigDrawIndexed = nullptr;

    phi::linked_pool<pso_node> mPool;
    phi::linked_pool<rt_pso_node> mPoolRaytracing;
    std::mutex mMutex;
};

}
