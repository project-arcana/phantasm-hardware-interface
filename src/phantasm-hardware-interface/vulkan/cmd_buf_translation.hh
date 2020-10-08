#pragma once

#include <clean-core/array.hh>

#include <phantasm-hardware-interface/commands.hh>

#include <phantasm-hardware-interface/vulkan/common/vk_incomplete_state_cache.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk
{
class ShaderViewPool;
class ResourcePool;
class PipelinePool;
class CommandListPool;
class AccelStructPool;
class QueryPool;

struct translator_global_memory
{
    void initialize(VkDevice device, ShaderViewPool* sv_pool, ResourcePool* resource_pool, PipelinePool* pso_pool, CommandListPool* cmd_pool, QueryPool* query_pool, AccelStructPool* as_pool)
    {
        this->device = device;
        this->pool_shader_views = sv_pool;
        this->pool_resources = resource_pool;
        this->pool_pipeline_states = pso_pool;
        this->pool_cmd_lists = cmd_pool;
        this->pool_queries = query_pool;
        this->pool_accel_structs = as_pool;
    }

    VkDevice device = nullptr;
    ShaderViewPool* pool_shader_views = nullptr;
    ResourcePool* pool_resources = nullptr;
    PipelinePool* pool_pipeline_states = nullptr;
    CommandListPool* pool_cmd_lists = nullptr;
    QueryPool* pool_queries = nullptr;
    AccelStructPool* pool_accel_structs = nullptr;

    translator_global_memory() = default;
};

/// responsible for filling command lists, 1 per thread
struct command_list_translator
{
    void initialize(VkDevice device, ShaderViewPool* sv_pool, ResourcePool* resource_pool, PipelinePool* pso_pool, CommandListPool* cmd_pool, QueryPool* query_pool, AccelStructPool* as_pool)
    {
        _globals.initialize(device, sv_pool, resource_pool, pso_pool, cmd_pool, query_pool, as_pool);
    }

    void translateCommandList(VkCommandBuffer list, handle::command_list list_handle, vk_incomplete_state_cache* state_cache, std::byte* buffer, size_t buffer_size);

    void execute(cmd::begin_render_pass const& begin_rp);

    void execute(cmd::draw const& draw);

    void execute(cmd::draw_indirect const& draw_indirect);

    void execute(cmd::dispatch const& dispatch);

    void execute(cmd::end_render_pass const& end_rp);

    void execute(cmd::transition_resources const& transition_res);

    void execute(cmd::transition_image_slices const& transition_images);

    void execute(cmd::barrier_uav const& barrier);

    void execute(cmd::copy_buffer const& copy_buf);

    void execute(cmd::copy_texture const& copy_tex);

    void execute(cmd::copy_buffer_to_texture const& copy_text);

    void execute(cmd::copy_texture_to_buffer const& copy_text);

    void execute(cmd::resolve_texture const& resolve);

    void execute(cmd::write_timestamp const& timestamp);

    void execute(cmd::resolve_queries const& resolve);

    void execute(cmd::begin_debug_label const& label);

    void execute(cmd::end_debug_label const&);

    void execute(cmd::update_bottom_level const& blas_update);

    void execute(cmd::update_top_level const& tlas_update);

    void execute(cmd::dispatch_rays const& dispatch_rays);

    void execute(cmd::clear_textures const& clear_tex);

private:
    void bind_shader_arguments(handle::pipeline_state pso, std::byte const* root_consts, cc::span<shader_argument const> shader_args, VkPipelineBindPoint bind_point);

    VkBuffer get_buffer_or_null(handle::resource buf) const;

private:
    // non-owning constant (global)
    translator_global_memory _globals;

    // non-owning dynamic
    vk_incomplete_state_cache* _state_cache = nullptr;
    VkCommandBuffer _cmd_list = nullptr;
    handle::command_list _cmd_list_handle = handle::null_command_list;

    // dynamic state
    struct
    {
        handle::pipeline_state pipeline_state = handle::null_pipeline_state;
        handle::resource index_buffer = handle::null_resource;
        handle::resource vertex_buffer = handle::null_resource;

        struct shader_arg_info
        {
            handle::shader_view sv = handle::null_shader_view;
            handle::resource cbv = handle::null_resource;
            unsigned cbv_offset = 0;

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

        VkRenderPass raw_render_pass = nullptr;
        VkFramebuffer raw_framebuffer = nullptr;
        VkDescriptorSet raw_sampler_descriptor_set = nullptr;
        VkPipelineLayout raw_pipeline_layout = nullptr;

        void reset()
        {
            pipeline_state = handle::null_pipeline_state;
            index_buffer = handle::null_resource;
            vertex_buffer = handle::null_resource;

            raw_render_pass = nullptr;
            raw_framebuffer = nullptr;
            set_pipeline_layout(nullptr);
        }

        void set_pipeline_layout(VkPipelineLayout raw)
        {
            // A new pipeline layout invalidates bound shader arguments
            for (auto& sa : shader_args)
                sa.reset();

            raw_sampler_descriptor_set = nullptr;
            raw_pipeline_layout = raw;
        }

        /// returns true if the argument is different from the currently bound one
        bool update_pipeline_layout(VkPipelineLayout raw)
        {
            if (raw_pipeline_layout != raw)
            {
                set_pipeline_layout(raw);
                return true;
            }
            return false;
        }

        /// returns true if the argument is different from the currently bound one
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
