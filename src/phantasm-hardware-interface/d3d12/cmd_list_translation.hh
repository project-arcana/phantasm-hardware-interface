#pragma once

#include <clean-core/array.hh>

#include <phantasm-hardware-interface/commands.hh>

#include <phantasm-hardware-interface/d3d12/common/d3d12_fwd.hh>
#include <phantasm-hardware-interface/d3d12/pools/linear_descriptor_allocator.hh>

namespace phi::detail
{
template <class StateT>
struct generic_incomplete_state_cache;
using incomplete_state_cache = generic_incomplete_state_cache<resource_state>;
}

namespace phi::d3d12
{
class ShaderViewPool;
class ResourcePool;
class PipelineStateObjectPool;
class CPUDescriptorLinearAllocator;
class AccelStructPool;

struct translator_thread_local_memory
{
    void initialize(ID3D12Device& device);

    CPUDescriptorLinearAllocator lin_alloc_rtvs;
    CPUDescriptorLinearAllocator lin_alloc_dsvs;
};

struct translator_global_memory
{
    void initialize(ID3D12Device* device, ShaderViewPool* sv_pool, ResourcePool* resource_pool, PipelineStateObjectPool* pso_pool, AccelStructPool* as_pool)
    {
        this->device = device;
        this->pool_shader_views = sv_pool;
        this->pool_resources = resource_pool;
        this->pool_pipeline_states = pso_pool;
        this->pool_accel_structs = as_pool;
    }

    ID3D12Device* device;
    ShaderViewPool* pool_shader_views;
    ResourcePool* pool_resources;
    PipelineStateObjectPool* pool_pipeline_states;
    AccelStructPool* pool_accel_structs;
};

/// responsible for filling command lists, 1 per thread
struct command_list_translator
{
    void initialize(ID3D12Device* device, ShaderViewPool* sv_pool, ResourcePool* resource_pool, PipelineStateObjectPool* pso_pool, AccelStructPool* as_pool);

    void translateCommandList(ID3D12GraphicsCommandList* list,
                              ID3D12GraphicsCommandList5* list5,
                              phi::detail::incomplete_state_cache* state_cache,
                              std::byte* buffer,
                              size_t buffer_size);

    void execute(cmd::begin_render_pass const& begin_rp);

    void execute(cmd::draw const& draw);

    void execute(cmd::dispatch const& dispatch);

    void execute(cmd::end_render_pass const& end_rp);

    void execute(cmd::transition_resources const& transition_res);

    void execute(cmd::transition_image_slices const& transition_images);

    void execute(cmd::copy_buffer const& copy_buf);

    void execute(cmd::copy_texture const& copy_text);

    void execute(cmd::copy_buffer_to_texture const& copy_text);

    void execute(cmd::resolve_texture const& resolve);

    void execute(cmd::debug_marker const& marker);

    void execute(cmd::update_bottom_level const& blas_update);

    void execute(cmd::update_top_level const& tlas_update);

    void execute(cmd::dispatch_rays const& dispatch_rays);

private:
    // non-owning constant (global)
    translator_global_memory _globals;

    // owning constant (thread local)
    translator_thread_local_memory _thread_local;

    // non-owning dynamic
    phi::detail::incomplete_state_cache* _state_cache = nullptr;
    ID3D12GraphicsCommandList* _cmd_list = nullptr;
    ID3D12GraphicsCommandList5* _cmd_list_5 = nullptr;

    // dynamic state
    struct
    {
        handle::pipeline_state pipeline_state;
        handle::resource index_buffer;
        handle::resource vertex_buffer;

        ID3D12RootSignature* raw_root_sig;

        struct shader_arg_info
        {
            handle::shader_view sv;
            handle::resource cbv;
            unsigned cbv_offset;

            void reset()
            {
                sv = handle::null_shader_view;
                cbv = handle::null_resource;
                cbv_offset = 0;
            }

            /// returns true if the argument is different from the currently bound one
            [[nodiscard]] bool update_shader_view(handle::shader_view new_sv)
            {
                if (sv != new_sv)
                {
                    sv = new_sv;
                    return true;
                }
                return false;
            }

            /// returns true if the argument is different from the currently bound one
            [[nodiscard]] bool update_cbv(handle::resource new_cbv, unsigned new_offset)
            {
                if (cbv_offset != new_offset || cbv != new_cbv)
                {
                    cbv_offset = new_offset;
                    cbv = new_cbv;
                    return true;
                }
                return false;
            }
        };

        cc::array<shader_arg_info, limits::max_shader_arguments> shader_args;

        void reset()
        {
            pipeline_state = handle::null_pipeline_state;
            index_buffer = handle::null_resource;
            vertex_buffer = handle::null_resource;

            set_root_sig(nullptr);
        }

        void set_root_sig(ID3D12RootSignature* raw)
        {
            // A new root signature invalidates bound shader arguments
            for (auto& sa : shader_args)
                sa.reset();

            raw_root_sig = raw;
        }

        /// returns true if the argument is different from the currently bound one
        [[nodiscard]] bool update_root_sig(ID3D12RootSignature* raw)
        {
            if (raw_root_sig != raw)
            {
                set_root_sig(raw);
                return true;
            }
            return false;
        }

        [[nodiscard]] bool update_pso(handle::pipeline_state new_pso)
        {
            if (pipeline_state != new_pso)
            {
                pipeline_state = new_pso;
                return true;
            }
            return false;
        }

    } _bound;
};

}
