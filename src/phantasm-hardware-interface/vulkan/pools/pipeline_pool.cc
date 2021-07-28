#include "pipeline_pool.hh"

#include <clean-core/defer.hh>

#include <phantasm-hardware-interface/common/log.hh>

#include <phantasm-hardware-interface/vulkan/common/util.hh>
#include <phantasm-hardware-interface/vulkan/loader/spirv_patch_util.hh>
#include <phantasm-hardware-interface/vulkan/render_pass_pipeline.hh>
#include <phantasm-hardware-interface/vulkan/shader.hh>

#define PHI_VK_DEBUG_PRINT_PSO_DESCRIPTOR_LAYOUT false

namespace
{
void verifyReflectionDataConsistencyInDebug(cc::span<const phi::vk::util::ReflectedDescriptorInfo> reflectedDescriptors,
                                            phi::arg::shader_arg_shapes argShapes,
                                            bool hasPushConstants,
                                            bool shouldHavePushConstants)
{
#ifdef CC_ENABLE_ASSERTIONS
    // Since we reflect all of the descriptor info from SPIR-V, arg shapes and root const flags are
    // not strictly required here, however in debug check if they are consistent
    phi::vk::util::warnIfReflectionIsInconsistent(reflectedDescriptors, argShapes);

    if (!!hasPushConstants != !!shouldHavePushConstants)
    {
        PHI_LOG_WARN("createPipelineState: Call {} root constants but SPIR-V reflection {}", shouldHavePushConstants ? "enables" : "disables",
                     hasPushConstants ? "finds push constants" : "finds none");
    }
#else
    (void)reflectedDescriptors;
    (void)argShapes;
    (void)hasPushConstants;
    (void)shouldHavePushConstants;
#endif
}
} // namespace

phi::handle::pipeline_state phi::vk::PipelinePool::createPipelineState(phi::arg::vertex_format vertex_format,
                                                                       phi::arg::framebuffer_config const& framebuffer_config,
                                                                       phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                       bool should_have_push_constants,
                                                                       phi::arg::graphics_shaders shader_stages,
                                                                       const phi::pipeline_config& primitive_config,
                                                                       cc::allocator* scratch_alloc,
                                                                       char const* dbg_name)
{
    // Patch and reflect SPIR-V binaries
    cc::capped_vector<util::PatchedShaderStage, 6> patched_shader_stages;
    cc::alloc_vector<util::ReflectedDescriptorInfo> shader_descriptor_ranges;
    bool has_push_constants = false;
    CC_DEFER
    {
        for (auto const& ps : patched_shader_stages)
            util::freePatchedShader(ps);
    };

    {
        util::ReflectedShaderInfo spirv_info;
        spirv_info.descriptor_infos.reset_reserve(scratch_alloc, shader_stages.size() * 8);

        for (auto const& shader : shader_stages)
        {
            patched_shader_stages.push_back(util::createPatchedShader(shader.binary.data, shader.binary.size, spirv_info, scratch_alloc));
        }

        shader_descriptor_ranges = util::mergeReflectedDescriptors(spirv_info.descriptor_infos, scratch_alloc);
        has_push_constants = spirv_info.has_push_constants;
    }

#if PHI_VK_DEBUG_PRINT_PSO_DESCRIPTOR_LAYOUT
    {
        PHI_LOG_TRACE("PSO Debug Descriptor Layout Print:");
        PHI_LOG_TRACE(R"(Graphics PSO "{}")", dbg_name);

        PHI_LOG_TRACE("Given Configuration:");
        PHI_LOG_TRACE("{} Shader args, Root consts {}", shader_arg_shapes.size(), should_have_push_constants ? "enabled" : "disabled");
        for (auto const& argshape : shader_arg_shapes)
        {
            PHI_LOG_TRACE("Shape: {} SRVs, {} UAVs, {} Samplers, CBV: {}", argshape.num_srvs, argshape.num_uavs, argshape.num_samplers, argshape.has_cbv);
        }

        PHI_LOG_TRACE("Reflected Configuration:");
        PHI_LOG_TRACE("Push consts {}", has_push_constants ? "present" : "not present");

        for (auto i = 0u; i < shader_descriptor_ranges.size(); ++i)
        {
            auto const& range = shader_descriptor_ranges[i];

            PHI_LOG_TRACE("Descriptor #{}: Set {}, Binding {}, Array size {}, Type {}", i, range.set, range.binding, range.binding_array_size, range.type);
        }
    }
#endif

    verifyReflectionDataConsistencyInDebug(shader_descriptor_ranges, shader_arg_shapes, has_push_constants, should_have_push_constants);

    pipeline_layout* layout;
    // Do things requiring synchronization
    {
        auto lg = std::lock_guard(mMutex);
        layout = mLayoutCache.getOrCreate(mDevice, shader_descriptor_ranges, has_push_constants);
    }

    uint32_t const pool_index = mPool.acquire();
    // Populate new node
    pso_node& new_node = mPool.get(pool_index);
    new_node.associated_pipeline_layout = layout;

    CC_ASSERT(primitive_config.samples > 0 && "invalid amount of MSAA samples");


    {
        // Create VkPipeline
        auto const vert_format_native = util::get_native_vertex_format(vertex_format.attributes);

        VkRenderPass dummy_render_pass = create_render_pass(mDevice, framebuffer_config, primitive_config);

        new_node.raw_pipeline = create_pipeline(mDevice, dummy_render_pass, new_node.associated_pipeline_layout->raw_layout, patched_shader_stages,
                                                primitive_config, vert_format_native, vertex_format.vertex_sizes_bytes, framebuffer_config);

        util::set_object_name(mDevice, new_node.raw_pipeline, "phi graphics pso %s", dbg_name ? dbg_name : "");

        vkDestroyRenderPass(mDevice, dummy_render_pass, nullptr);
    }

    return {pool_index};
}

phi::handle::pipeline_state phi::vk::PipelinePool::createComputePipelineState(phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                              arg::shader_binary compute_shader,
                                                                              bool should_have_push_constants,
                                                                              cc::allocator* scratch_alloc,
                                                                              char const* dbg_name)
{
    // Patch and reflect SPIR-V binary
    util::PatchedShaderStage patched_shader_stage;
    cc::alloc_vector<util::ReflectedDescriptorInfo> shader_descriptor_ranges;
    bool has_push_constants = false;
    CC_DEFER { util::freePatchedShader(patched_shader_stage); };

    {
        util::ReflectedShaderInfo spirv_info;
        spirv_info.descriptor_infos.reset_reserve(scratch_alloc, 10);

        patched_shader_stage = util::createPatchedShader(compute_shader.data, compute_shader.size, spirv_info, scratch_alloc);
        shader_descriptor_ranges = util::mergeReflectedDescriptors(spirv_info.descriptor_infos, scratch_alloc);
        has_push_constants = spirv_info.has_push_constants;

        verifyReflectionDataConsistencyInDebug(shader_descriptor_ranges, shader_arg_shapes, has_push_constants, should_have_push_constants);
    }


    pipeline_layout* layout;
    // Do things requiring synchronization
    {
        auto lg = std::lock_guard(mMutex);
        layout = mLayoutCache.getOrCreate(mDevice, shader_descriptor_ranges, has_push_constants);
    }

    uint32_t const pool_index = mPool.acquire();

    // Populate new node
    pso_node& new_node = mPool.get(pool_index);
    new_node.associated_pipeline_layout = layout;


    new_node.raw_pipeline = create_compute_pipeline(mDevice, new_node.associated_pipeline_layout->raw_layout, patched_shader_stage);
    util::set_object_name(mDevice, new_node.raw_pipeline, "phi compute pso %s", dbg_name ? dbg_name : "");

    return {pool_index};
}

phi::handle::pipeline_state phi::vk::PipelinePool::createRaytracingPipelineState(cc::span<const arg::raytracing_shader_library> libraries,
                                                                                 cc::span<const arg::raytracing_argument_association> arg_assocs,
                                                                                 cc::span<const arg::raytracing_hit_group> hit_groups,
                                                                                 unsigned max_recursion,
                                                                                 unsigned max_payload_size_bytes,
                                                                                 unsigned max_attribute_size_bytes,
                                                                                 cc::allocator* scratch_alloc)
{
    CC_ASSERT(libraries.size() > 0 && arg_assocs.size() <= limits::max_raytracing_argument_assocs && "zero libraries or too many argument associations");
    CC_ASSERT(hit_groups.size() <= limits::max_raytracing_hit_groups && "too many hit groups");

    patched_shader_intermediates shader_intermediates;
    shader_intermediates.initialize_from_libraries(mDevice, libraries, scratch_alloc);
    CC_DEFER { shader_intermediates.free(mDevice); };
    // util::logSpirvDescriptorInfo(shader_intermediates.sorted_merged_descriptor_infos);

    // verifying the descriptor ranges reflected here is much more involved than in a graphics / compute setting, skipping for now

    pipeline_layout* layout;

    // Do things requiring synchronization
    {
        auto lg = std::lock_guard(mMutex);
        layout = mLayoutCache.getOrCreate(mDevice, shader_intermediates.sorted_merged_descriptor_infos, shader_intermediates.has_root_constants);
    }

    uint32_t const pool_index = mPool.acquire();

    // Populate new node
    pso_node& new_node = mPool.get(pool_index);
    new_node.associated_pipeline_layout = layout;
    new_node.raw_pipeline = create_raytracing_pipeline(mDevice, shader_intermediates, new_node.associated_pipeline_layout->raw_layout, arg_assocs,
                                                       hit_groups, max_recursion, max_payload_size_bytes, max_attribute_size_bytes, scratch_alloc);

    return {pool_index};
}

void phi::vk::PipelinePool::free(phi::handle::pipeline_state ps)
{
    // This requires no synchronization, as VMA internally syncs
    pso_node& freed_node = mPool.get(ps._value);
    vkDestroyPipeline(mDevice, freed_node.raw_pipeline, nullptr);

    mPool.release(ps._value);
}

void phi::vk::PipelinePool::initialize(VkDevice device, unsigned max_num_psos, cc::allocator* static_alloc)
{
    mDevice = device;
    mPool.initialize(max_num_psos, static_alloc);

    // almost arbitrary, revisit upon crashes
    mLayoutCache.initialize(max_num_psos, static_alloc);
    mRenderPassCache.initialize(max_num_psos, static_alloc);

    // precise
    mDescriptorAllocator.initialize(mDevice, 0, 0, 0, max_num_psos);
}

void phi::vk::PipelinePool::destroy()
{
    auto num_leaks = 0;

    mPool.iterate_allocated_nodes([&](pso_node& leaked_node) {
        ++num_leaks;
        vkDestroyPipeline(mDevice, leaked_node.raw_pipeline, nullptr);
    });

    if (num_leaks > 0)
    {
        PHI_LOG("leaked {} handle::pipeline_state object{}", num_leaks, (num_leaks == 1 ? "" : "s"));
    }

    mLayoutCache.destroy(mDevice);
    mRenderPassCache.destroy(mDevice);
    mDescriptorAllocator.destroy();
}

VkRenderPass phi::vk::PipelinePool::getOrCreateRenderPass(const phi::cmd::begin_render_pass& brp_cmd, int num_samples, cc::span<const format> rt_formats)
{
    // NOTE: This is a mutex acquire on the hot path (in cmd::begin_render_pass)
    // Its not quite trivial to fix this, all solutions involve tradeoffs,
    // either restricting API free-threadedness, or making clear types part of the handle::pipeline_state
    auto lg = std::lock_guard(mMutex);
    return mRenderPassCache.getOrCreate(mDevice, brp_cmd, num_samples, rt_formats);
}
