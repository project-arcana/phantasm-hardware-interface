#pragma once

#include <mutex>

#include <clean-core/alloc_array.hh>
#include <clean-core/atomic_linked_pool.hh>

#include <phantasm-hardware-interface/arguments.hh>
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

    [[nodiscard]] handle::pipeline_state createRaytracingPipelineState(cc::span<arg::raytracing_shader_library const> libraries,
                                                                       cc::span<arg::raytracing_argument_association const> arg_assocs,
                                                                       cc::span<arg::raytracing_hit_group const> hit_groups,
                                                                       unsigned max_recursion,
                                                                       unsigned max_payload_size_bytes,
                                                                       unsigned max_attribute_size_bytes,
                                                                       cc::allocator* scratch_alloc);

    void free(handle::pipeline_state ps);

public:
    struct pso_argument_info
    {
        // when writing RT shader tables, it does not matter how many SRV/UAVs or Samplers any argument has,
        // only if it has any at all (writes a single descriptor table pointer, 8 byte)
        // the info in this struct is purely for verification when writing shader table records, to match inputs against argument assocs
        // one per identifiable shader, and one per hitgroup

        using flag_t = uint16_t;

        enum e_bitmasks
        {
            eb_has_cbv,
            eb_has_srv_uav,
            eb_has_sampler,
            eb_arg_stride,

            eb_no_rootsig_available = sizeof(flag_t) * 8 - 2,
            eb_has_root_constants = sizeof(flag_t) * 8 - 1
        };

        flag_t flags = 0;

        void initialize(arg::shader_arg_shapes shapes, bool root_consts_present);
        void initialize_no_rootsig();

        bool has_no_rootsig() const { return cc::has_bit(flags, eb_no_rootsig_available); }
        bool has_root_consts() const { return cc::has_bit(flags, eb_has_root_constants); }

        bool has_cbv(unsigned arg_index) const { return cc::has_bit(flags, eb_arg_stride * arg_index + eb_has_cbv); }
        bool has_srv_uav(unsigned arg_index) const { return cc::has_bit(flags, eb_arg_stride * arg_index + eb_has_srv_uav); }
        bool has_sampler(unsigned arg_index) const { return cc::has_bit(flags, eb_arg_stride * arg_index + eb_has_sampler); }

        bool is_matching_inputs(arg::shader_arg_shapes shapes, unsigned root_constant_bytes) const;
    };


    struct pso_node
    {
        ID3D12PipelineState* raw_pso;
        root_signature* associated_root_sig;
        D3D12_PRIMITIVE_TOPOLOGY primitive_topology;
    };

    struct rt_pso_node
    {
        ID3D12StateObject* raw_state_object;
        ID3D12StateObjectProperties* raw_state_object_props; // currently unused after creation, could be removed
        cc::capped_vector<root_signature*, limits::max_raytracing_argument_assocs> associated_root_signatures;

        struct export_info
        {
            std::byte shader_identifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
            pso_argument_info arg_info;
        };

        cc::alloc_array<export_info> identifiable_shader_infos; // infos about the identifiable shaders of this PSO (in order)
        cc::alloc_array<export_info> hitgroup_infos;            // infos about the hitgroups of this PSO (in order)
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

    cc::atomic_linked_pool<pso_node> mPool;
    cc::atomic_linked_pool<rt_pso_node> mPoolRaytracing;
    std::mutex mMutex;
};

}
