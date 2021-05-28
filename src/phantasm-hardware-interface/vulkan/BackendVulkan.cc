#include "BackendVulkan.hh"

#ifdef PHI_HAS_OPTICK
#include <optick/optick.h>
#endif

#include <clean-core/allocator.hh>
#include <clean-core/array.hh>
#include <clean-core/defer.hh>
#include <clean-core/native/win32_util.hh>

#include <rich-log/logger.hh>

#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/config.hh>

#include "cmd_buf_translation.hh"
#include "common/debug_callback.hh"
#include "common/verify.hh"
#include "common/vk_format.hh"
#include "gpu_choice_util.hh"
#include "layer_extension_util.hh"
#include "loader/volk.hh"
#include "resources/transition_barrier.hh"

namespace phi::vk
{
struct BackendVulkan::per_thread_component
{
    command_list_translator translator;
    CommandAllocatorsPerThread cmdListAllocator;
    cc::alloc_array<std::byte> threadLocalScratchAllocMemory;
    cc::linear_allocator threadLocalScratchAlloc;
};
} // namespace phi::vk

namespace
{
constexpr VkPipelineStageFlags const gc_wait_dst_masks[8]
    = {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
}

void phi::vk::BackendVulkan::initialize(const backend_config& config_arg)
{
    // enable colors as rich-log is used by this library
    rlog::enable_win32_colors();

    PHI_VK_VERIFY_SUCCESS(volkInitialize());

    // copy explicitly for modifications
    backend_config config = config_arg;

    mDiagnostics.init();
    if (mDiagnostics.is_renderdoc_present() && config.validation >= validation_level::on)
    {
        PHI_LOG("Validation layers requested while running RenderDoc, disabling due to known crashes");
        config.validation = validation_level::off;
    }

    auto const active_lay_ext = getUsedInstanceExtensions(getAvailableInstanceExtensions(), config);

    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Phantasm Hardware Interface Application";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Phantasm Hardware Interface";
    app_info.engineVersion = VK_MAKE_VERSION(1, 2, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = uint32_t(active_lay_ext.extensions.size());
    instance_info.ppEnabledExtensionNames = active_lay_ext.extensions.empty() ? nullptr : active_lay_ext.extensions.data();
    instance_info.enabledLayerCount = uint32_t(active_lay_ext.layers.size());
    instance_info.ppEnabledLayerNames = active_lay_ext.layers.empty() ? nullptr : active_lay_ext.layers.data();

    cc::capped_vector<VkValidationFeatureEnableEXT, 4> extended_validation_enables;

    if (config.validation >= validation_level::on_extended)
    {
        // Enable GPU based validation (GBV)
        extended_validation_enables.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
        extended_validation_enables.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
    }

    if (config.native_features & backend_config::native_feature_vk_best_practices_layer)
    {
        if (config.validation < validation_level::on)
        {
            PHI_LOG_WARN("Vulkan best practices layer requires validation_level::on or higher (native_feature_vk_best_practices_layer)");
        }
        else
        {
            PHI_LOG("Vulkan best practices layer enabled (native_feature_vk_best_practices_layer)");
            extended_validation_enables.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
        }
    }

    VkValidationFeaturesEXT extended_validation_features = {};

    if (extended_validation_enables.size() > 0)
    {
        extended_validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        extended_validation_features.enabledValidationFeatureCount = uint32_t(extended_validation_enables.size());
        extended_validation_features.pEnabledValidationFeatures = extended_validation_enables.data();

        instance_info.pNext = &extended_validation_features;
    }

    // Create the instance
    VkResult create_res = vkCreateInstance(&instance_info, nullptr, &mInstance);

    // TODO: More fine-grained error handling
    PHI_VK_ASSERT_SUCCESS(create_res);

    // Load instance-based Vulkan entrypoints
    volkLoadInstanceOnly(mInstance);

    if (config.validation != validation_level::off)
    {
        // Debug callback
        createDebugMessenger();
    }

    // GPU choice and device init
    {
        auto const vk_gpu_infos = get_all_vulkan_gpu_infos(mInstance);
        auto const gpu_infos = get_available_gpus(vk_gpu_infos);
        auto const chosen_index = getPreferredGPU(gpu_infos, config.adapter);
        CC_RUNTIME_ASSERT(chosen_index < gpu_infos.size() && "Found no GPU candidates");

        auto const& chosen_gpu = gpu_infos[chosen_index];
        auto const& chosen_vk_gpu = vk_gpu_infos[chosen_gpu.index];

        mDevice.initialize(chosen_vk_gpu, config);

        // Load device-based Vulkan entrypoints
        volkLoadDevice(mDevice.getDevice());

        printStartupMessage(gpu_infos, chosen_index, config, false);

        if (config.print_startup_message)
        {
            PHI_LOG("   compiled with vulkan sdk v{}.{}.{}", vkver::major, vkver::minor, vkver::patch);
        }

        mGPUInfo = gpu_infos[chosen_index];
    }

    // Pool init
    mPoolPipelines.initialize(mDevice.getDevice(), config.max_num_pipeline_states, config.static_allocator);
    mPoolResources.initialize(mDevice.getPhysicalDevice(), mDevice.getDevice(), config.max_num_resources, config.max_num_swapchains, config.static_allocator);
    mPoolShaderViews.initialize(mDevice.getDevice(), &mPoolResources, &mPoolAccelStructs, config.max_num_cbvs, config.max_num_srvs,
                                config.max_num_uavs, config.max_num_samplers, config.static_allocator);
    mPoolFences.initialize(mDevice.getDevice(), config.max_num_fences, config.static_allocator);
    mPoolQueries.initialize(mDevice.getDevice(), config.num_timestamp_queries, config.num_occlusion_queries, config.num_pipeline_stat_queries, config.static_allocator);

    if (isRaytracingEnabled())
    {
        mPoolAccelStructs.initialize(mDevice.getDevice(), &mPoolResources, config.max_num_accel_structs, config.static_allocator);
        mShaderTableCtor.initialize(mDevice.getDevice(), &mPoolShaderViews, &mPoolResources, &mPoolPipelines, &mPoolAccelStructs);
    }

    mPoolSwapchains.initialize(mInstance, mDevice, config);

    // Per-thread components and command list pool
    {
        mThreadAssociation.initialize();

        mThreadComponentAlloc = config.static_allocator;
        mThreadComponents = config.static_allocator->new_array_sized<per_thread_component>(config.num_threads);
        mNumThreadComponents = config.num_threads;

        cc::alloc_array<CommandAllocatorsPerThread*> thread_allocator_ptrs(mNumThreadComponents, config.dynamic_allocator);

        for (auto i = 0u; i < mNumThreadComponents; ++i)
        {
            auto& thread_comp = mThreadComponents[i];
            thread_comp.translator.initialize(mDevice.getDevice(), &mPoolShaderViews, &mPoolResources, &mPoolPipelines, &mPoolCmdLists, &mPoolQueries,
                                              &mPoolAccelStructs, mDevice.hasRaytracing());
            thread_allocator_ptrs[i] = &thread_comp.cmdListAllocator;

            // 5 MB scratch alloc per thread
            thread_comp.threadLocalScratchAllocMemory.reset(config.static_allocator, 1024 * 1024 * 5);
            thread_comp.threadLocalScratchAlloc = cc::linear_allocator(thread_comp.threadLocalScratchAllocMemory);
        }

        mPoolCmdLists.initialize(mDevice,                                                                                               //
                                 int(config.num_direct_cmdlist_allocators_per_thread), int(config.num_direct_cmdlists_per_allocator),   //
                                 int(config.num_compute_cmdlist_allocators_per_thread), int(config.num_compute_cmdlists_per_allocator), //
                                 int(config.num_copy_cmdlist_allocators_per_thread), int(config.num_copy_cmdlists_per_allocator),
                                 config.max_num_unique_transitions_per_cmdlist, //
                                 thread_allocator_ptrs, config.static_allocator, config.dynamic_allocator);
    }

#ifdef PHI_HAS_OPTICK
    {
        VkDevice dev = mDevice.getDevice();
        VkPhysicalDevice physDev = mDevice.getPhysicalDevice();
        VkQueue dirQueue = mDevice.getRawQueue(phi::queue_type::direct);
        uint32_t dirQueueIdx = uint32_t(mDevice.getQueueFamilyDirect());

        OPTICK_GPU_INIT_VULKAN(&dev, &physDev, &dirQueue, &dirQueueIdx, 1, nullptr);
    }
#endif
}

void phi::vk::BackendVulkan::destroy()
{
    if (mInstance != nullptr)
    {
        flushGPU();

        mDiagnostics.free();

        mPoolSwapchains.destroy();

        mPoolAccelStructs.destroy();
        mPoolQueries.destroy(mDevice.getDevice());
        mPoolFences.destroy();
        mPoolShaderViews.destroy();
        mPoolCmdLists.destroy();
        mPoolPipelines.destroy();
        mPoolResources.destroy();

        for (auto i = 0u; i < mNumThreadComponents; ++i)
        {
            auto& thread_comp = mThreadComponents[i];
            thread_comp.cmdListAllocator.destroy(mDevice.getDevice());
            thread_comp.threadLocalScratchAllocMemory = {};
        }
        static_cast<cc::allocator*>(mThreadComponentAlloc)->delete_array_sized(mThreadComponents, mNumThreadComponents);

        mDevice.destroy();

        if (mDebugMessenger != nullptr)
            vkDestroyDebugUtilsMessengerEXT(mInstance, mDebugMessenger, nullptr);

        vkDestroyInstance(mInstance, nullptr);
        mInstance = nullptr;

        mThreadAssociation.destroy();
    }
}

phi::vk::BackendVulkan::~BackendVulkan() { destroy(); }

phi::handle::swapchain phi::vk::BackendVulkan::createSwapchain(const phi::window_handle& window_handle, tg::isize2 initial_size, phi::present_mode mode, uint32_t num_backbuffers)
{
    return mPoolSwapchains.createSwapchain(window_handle, initial_size.width, initial_size.height, num_backbuffers, mode);
}

void phi::vk::BackendVulkan::free(phi::handle::swapchain sc) { mPoolSwapchains.free(sc); }

phi::handle::resource phi::vk::BackendVulkan::acquireBackbuffer(handle::swapchain sc)
{
    auto const swapchain_index = mPoolSwapchains.getSwapchainIndex(sc);
    auto const& swapchain = mPoolSwapchains.get(sc);
    auto const prev_backbuffer_index = swapchain.active_image_index;
    bool const acquire_success = mPoolSwapchains.acquireBackbuffer(sc);

    if (!acquire_success)
    {
        return handle::null_resource;
    }
    else
    {
        resource_state prev_state;
        auto const& current_backbuffer = swapchain.backbuffers[swapchain.active_image_index];
        auto const res = mPoolResources.injectBackbufferResource(swapchain_index, current_backbuffer.image, current_backbuffer.state,
                                                                 current_backbuffer.view, swapchain.backbuf_width, swapchain.backbuf_height, prev_state);

        mPoolSwapchains.setBackbufferState(sc, prev_backbuffer_index, prev_state);
        return res;
    }
}

void phi::vk::BackendVulkan::present(phi::handle::swapchain sc) { mPoolSwapchains.present(sc); }

void phi::vk::BackendVulkan::onResize(handle::swapchain sc, tg::isize2 size)
{
    flushGPU();
    mPoolSwapchains.onResize(sc, size.width, size.height);
}

phi::format phi::vk::BackendVulkan::getBackbufferFormat(phi::handle::swapchain sc) const
{
    return util::to_pr_format(mPoolSwapchains.get(sc).backbuf_format.format);
}

phi::handle::resource phi::vk::BackendVulkan::createTexture(arg::texture_description const& desc, char const* debug_name)
{
    return mPoolResources.createTexture(desc, debug_name);
}

phi::handle::resource phi::vk::BackendVulkan::createBuffer(arg::buffer_description const& desc, char const* debug_name)
{
    return mPoolResources.createBuffer(desc, debug_name);
}

std::byte* phi::vk::BackendVulkan::mapBuffer(phi::handle::resource res, int begin, int end) { return mPoolResources.mapBuffer(res, begin, end); }

void phi::vk::BackendVulkan::unmapBuffer(phi::handle::resource res, int begin, int end) { return mPoolResources.unmapBuffer(res, begin, end); }

void phi::vk::BackendVulkan::free(phi::handle::resource res) { mPoolResources.free(res); }

void phi::vk::BackendVulkan::freeRange(cc::span<const phi::handle::resource> resources) { mPoolResources.free(resources); }

phi::handle::shader_view phi::vk::BackendVulkan::createShaderView(cc::span<const phi::resource_view> srvs,
                                                                  cc::span<const phi::resource_view> uavs,
                                                                  cc::span<const phi::sampler_config> samplers,
                                                                  bool usage_compute)
{
    auto const res = mPoolShaderViews.create(srvs, uavs, samplers, usage_compute, getCurrentScratchAlloc());
    resetCurrentScratchAlloc();
    return res;
}

phi::handle::shader_view phi::vk::BackendVulkan::createEmptyShaderView(arg::shader_view_description const& desc, bool usage_compute)
{
    auto const res = mPoolShaderViews.createEmpty(desc, usage_compute);
    return res;
}

void phi::vk::BackendVulkan::writeShaderViewSRVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> srvs)
{
    mPoolShaderViews.writeShaderViewSRVs(sv, offset, srvs, getCurrentScratchAlloc());
    resetCurrentScratchAlloc();
}

void phi::vk::BackendVulkan::writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs)
{
    mPoolShaderViews.writeShaderViewUAVs(sv, offset, uavs, getCurrentScratchAlloc());
    resetCurrentScratchAlloc();
}

void phi::vk::BackendVulkan::writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers)
{
    mPoolShaderViews.writeShaderViewSamplers(sv, offset, samplers, getCurrentScratchAlloc());
    resetCurrentScratchAlloc();
}

void phi::vk::BackendVulkan::free(phi::handle::shader_view sv) { mPoolShaderViews.free(sv); }

void phi::vk::BackendVulkan::freeRange(cc::span<const phi::handle::shader_view> svs) { mPoolShaderViews.free(svs); }

phi::handle::pipeline_state phi::vk::BackendVulkan::createPipelineState(phi::arg::vertex_format vertex_format,
                                                                        const phi::arg::framebuffer_config& framebuffer_conf,
                                                                        phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                        bool has_root_constants,
                                                                        phi::arg::graphics_shaders shaders,
                                                                        const phi::pipeline_config& primitive_config,
                                                                        char const* debug_name)
{
    auto const res = mPoolPipelines.createPipelineState(vertex_format, framebuffer_conf, shader_arg_shapes, has_root_constants, shaders,
                                                        primitive_config, getCurrentScratchAlloc(), debug_name);
    resetCurrentScratchAlloc();
    return res;
}

phi::handle::pipeline_state phi::vk::BackendVulkan::createPipelineState(const phi::arg::graphics_pipeline_state_description& description, char const* debug_name)
{
    auto const res = mPoolPipelines.createPipelineState(description.vertices, description.framebuffer, description.shader_arg_shapes, description.has_root_constants,
                                                        description.shader_binaries, description.config, getCurrentScratchAlloc(), debug_name);
    resetCurrentScratchAlloc();
    return res;
}

phi::handle::pipeline_state phi::vk::BackendVulkan::createComputePipelineState(phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                               phi::arg::shader_binary shader,
                                                                               bool has_root_constants,
                                                                               char const* debug_name)
{
    auto const res = mPoolPipelines.createComputePipelineState(shader_arg_shapes, shader, has_root_constants, getCurrentScratchAlloc(), debug_name);
    resetCurrentScratchAlloc();
    return res;
}

phi::handle::pipeline_state phi::vk::BackendVulkan::createComputePipelineState(const phi::arg::compute_pipeline_state_description& description, char const* debug_name)
{
    auto const res = mPoolPipelines.createComputePipelineState(description.shader_arg_shapes, description.shader, description.has_root_constants,
                                                               getCurrentScratchAlloc(), debug_name);
    resetCurrentScratchAlloc();
    return res;
}

void phi::vk::BackendVulkan::free(phi::handle::pipeline_state ps) { mPoolPipelines.free(ps); }

phi::handle::command_list phi::vk::BackendVulkan::recordCommandList(std::byte const* buffer, size_t size, queue_type queue)
{
    // possibly fall back to a direct queue
    queue = mDevice.getQueueTypeOrFallback(queue);

    auto& thread_comp = getCurrentThreadComponent();

    VkCommandBuffer raw_list;
    auto const res = mPoolCmdLists.create(raw_list, thread_comp.cmdListAllocator, queue);
    thread_comp.translator.translateCommandList(raw_list, res, mPoolCmdLists.getStateCache(res), buffer, size);
    return res;
}

void phi::vk::BackendVulkan::discard(cc::span<const phi::handle::command_list> cls) { mPoolCmdLists.freeAndDiscard(cls); }

void phi::vk::BackendVulkan::submit(cc::span<const phi::handle::command_list> cls,
                                    phi::queue_type queue,
                                    cc::span<const phi::fence_operation> fence_waits_before,
                                    cc::span<const phi::fence_operation> fence_signals_after)
{
    cc::alloc_vector<VkCommandBuffer> cmd_bufs_to_submit;
    cmd_bufs_to_submit.reset_reserve(getCurrentScratchAlloc(), cls.size() * 2);

    cc::alloc_vector<handle::command_list> barrier_lists;
    barrier_lists.reset_reserve(getCurrentScratchAlloc(), cls.size());

    CC_DEFER { resetCurrentScratchAlloc(); };

    // possibly fall back to a direct queue
    queue = mDevice.getQueueTypeOrFallback(queue);

    auto& thread_comp = getCurrentThreadComponent();

    for (handle::command_list const cl : cls)
    {
        // silently ignore invalid handles
        if (cl == handle::null_command_list)
            continue;

        auto const* const state_cache = mPoolCmdLists.getStateCache(cl);
        barrier_bundle<32, 32, 32> barriers;

        for (auto i = 0u; i < state_cache->num_entries; ++i)
        {
            auto const& entry = state_cache->entries[i];
            auto const master_before = mPoolResources.getResourceState(entry.ptr);

            if (master_before != entry.required_initial)
            {
                auto const master_dep_before = mPoolResources.getResourceStageDependency(entry.ptr);

                // transition to the state required as the initial one
                state_change const change = state_change(master_before, entry.required_initial, master_dep_before, entry.initial_dependency);

                if (mPoolResources.isImage(entry.ptr))
                {
                    auto const& img_info = mPoolResources.getImageInfo(entry.ptr);
                    barriers.add_image_barrier(img_info.raw_image, change, util::to_native_image_aspect(img_info.pixel_format));
                }
                else
                {
                    auto const& buf_info = mPoolResources.getBufferInfo(entry.ptr);
                    barriers.add_buffer_barrier(buf_info.raw_buffer, change, buf_info.width);
                }
            }

            // set the master state to the one in which this resource is left
            mPoolResources.setResourceState(entry.ptr, entry.current, entry.current_dependency);
        }

        // special barrier-only command list inserted before the proper one
        if (!barriers.empty())
        {
            VkCommandBuffer t_cmd_list;
            barrier_lists.push_back(mPoolCmdLists.create(t_cmd_list, thread_comp.cmdListAllocator, queue));
            barriers.record(t_cmd_list);
            vkEndCommandBuffer(t_cmd_list);
            cmd_bufs_to_submit.push_back(t_cmd_list);
        }

        cmd_bufs_to_submit.push_back(mPoolCmdLists.getRawBuffer(cl));
    }

    // submission

    constexpr uint32_t c_max_num_signals_waits = 8;

    uint64_t wait_values[c_max_num_signals_waits];
    VkSemaphore wait_semaphores[c_max_num_signals_waits];

    uint64_t signal_values[c_max_num_signals_waits];
    VkSemaphore signal_semaphores[c_max_num_signals_waits];

    CC_ASSERT(fence_waits_before.size() <= c_max_num_signals_waits && "too many fence waits");
    CC_ASSERT(fence_signals_after.size() <= c_max_num_signals_waits && "too many fence signals");

    for (auto i = 0u; i < fence_waits_before.size(); ++i)
    {
        wait_values[i] = fence_waits_before[i].value;
        wait_semaphores[i] = mPoolFences.get(fence_waits_before[i].fence);
    }

    for (auto i = 0u; i < fence_signals_after.size(); ++i)
    {
        signal_values[i] = fence_signals_after[i].value;
        signal_semaphores[i] = mPoolFences.get(fence_signals_after[i].fence);
    }

    VkTimelineSemaphoreSubmitInfoKHR timeline_info = {};
    timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
    timeline_info.waitSemaphoreValueCount = uint32_t(fence_waits_before.size());
    timeline_info.pWaitSemaphoreValues = fence_waits_before.empty() ? nullptr : wait_values;
    timeline_info.signalSemaphoreValueCount = uint32_t(fence_signals_after.size());
    timeline_info.pSignalSemaphoreValues = fence_signals_after.empty() ? nullptr : signal_values;

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &timeline_info;
    // command buffers
    submit_info.commandBufferCount = uint32_t(cmd_bufs_to_submit.size());
    submit_info.pCommandBuffers = cmd_bufs_to_submit.data();
    // wait semaphores
    submit_info.waitSemaphoreCount = uint32_t(fence_waits_before.size());
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = gc_wait_dst_masks;
    // signal semaphores
    submit_info.signalSemaphoreCount = uint32_t(fence_signals_after.size());
    submit_info.pSignalSemaphores = signal_semaphores;


    VkQueue const submit_queue = mDevice.getRawQueue(queue);
    VkFence submit_fence;
    auto const submit_fence_index = mPoolCmdLists.acquireFence(submit_fence);
    PHI_VK_VERIFY_SUCCESS(vkQueueSubmit(submit_queue, 1, &submit_info, submit_fence));

    cc::array<cc::span<handle::command_list const>, 2> submit_spans = {barrier_lists, cls};
    mPoolCmdLists.freeOnSubmit(submit_spans, submit_fence_index);
}

phi::handle::fence phi::vk::BackendVulkan::createFence() { return mPoolFences.createFence(); }

uint64_t phi::vk::BackendVulkan::getFenceValue(phi::handle::fence fence) { return mPoolFences.getValue(fence); }

void phi::vk::BackendVulkan::signalFenceCPU(phi::handle::fence fence, uint64_t new_value) { mPoolFences.signalCPU(fence, new_value); }

void phi::vk::BackendVulkan::waitFenceCPU(phi::handle::fence fence, uint64_t wait_value) { mPoolFences.waitCPU(fence, wait_value); }

void phi::vk::BackendVulkan::free(cc::span<const phi::handle::fence> fences) { mPoolFences.free(fences); }

phi::handle::query_range phi::vk::BackendVulkan::createQueryRange(phi::query_type type, uint32_t size) { return mPoolQueries.create(type, size); }

void phi::vk::BackendVulkan::free(phi::handle::query_range query_range) { mPoolQueries.free(query_range); }

phi::handle::pipeline_state phi::vk::BackendVulkan::createRaytracingPipelineState(const arg::raytracing_pipeline_state_description& description)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    auto const res = mPoolPipelines.createRaytracingPipelineState(description.libraries, description.argument_associations, description.hit_groups,
                                                                  description.max_recursion, description.max_payload_size_bytes,
                                                                  description.max_attribute_size_bytes, getCurrentScratchAlloc());
    resetCurrentScratchAlloc();
    return res;
}

phi::handle::accel_struct phi::vk::BackendVulkan::createTopLevelAccelStruct(uint32_t num_instances, accel_struct_build_flags_t flags)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    return mPoolAccelStructs.createTopLevelAS(num_instances);
}

phi::handle::accel_struct phi::vk::BackendVulkan::createBottomLevelAccelStruct(cc::span<const phi::arg::blas_element> elements,
                                                                               accel_struct_build_flags_t flags,
                                                                               uint64_t* out_native_handle)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    auto const res = mPoolAccelStructs.createBottomLevelAS(elements, flags);

    if (out_native_handle != nullptr)
        *out_native_handle = mPoolAccelStructs.getNode(res).raw_as_handle;

    return res;
}

uint64_t phi::vk::BackendVulkan::getAccelStructNativeHandle(phi::handle::accel_struct as)
{
    CC_ASSERT(false && "getAccelStructNativeHandle unimplemented");
    return 0;
}

phi::shader_table_strides phi::vk::BackendVulkan::calculateShaderTableStrides(const arg::shader_table_record& ray_gen_record,
                                                                              phi::arg::shader_table_records miss_records,
                                                                              phi::arg::shader_table_records hit_group_records,
                                                                              phi::arg::shader_table_records callable_records)
{
    CC_ASSERT(false && "calculateShaderTableSize unimplemented");
    return {};
}

void phi::vk::BackendVulkan::writeShaderTable(std::byte* dest, handle::pipeline_state pso, uint32_t stride, arg::shader_table_records records)
{
    CC_ASSERT(false && "writeShaderTable unimplemented");
}

void phi::vk::BackendVulkan::free(phi::handle::accel_struct as)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    mPoolAccelStructs.free(as);
}

void phi::vk::BackendVulkan::freeRange(cc::span<const phi::handle::accel_struct> as)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    mPoolAccelStructs.free(as);
}

phi::arg::resource_description const& phi::vk::BackendVulkan::getResourceDescription(handle::resource res) const
{
    return mPoolResources.getResourceDescription(res);
}

phi::arg::texture_description const& phi::vk::BackendVulkan::getResourceTextureDescription(handle::resource res) const
{
    return mPoolResources.getTextureDescription(res);
}

phi::arg::buffer_description const& phi::vk::BackendVulkan::getResourceBufferDescription(handle::resource res) const
{
    return mPoolResources.getBufferDescription(res);
}

void phi::vk::BackendVulkan::setDebugName(phi::handle::resource res, cc::string_view name)
{
    mPoolResources.setDebugName(res, name.data(), uint32_t(name.length()));
}

bool phi::vk::BackendVulkan::startForcedDiagnosticCapture() { return mDiagnostics.start_capture(); }

bool phi::vk::BackendVulkan::endForcedDiagnosticCapture() { return mDiagnostics.end_capture(); }

uint64_t phi::vk::BackendVulkan::getGPUTimestampFrequency() const
{
    float const nanoseconds_per_timestamp = mDevice.getDeviceProperties().limits.timestampPeriod;
    uint64_t const timestamps_per_microsecond = uint64_t(1000.f / nanoseconds_per_timestamp);
    // us -> ms -> s (Hz)
    return timestamps_per_microsecond * 1000 * 1000;
}

bool phi::vk::BackendVulkan::isRaytracingEnabled() const { return mDevice.hasRaytracing(); }

phi::backend_type phi::vk::BackendVulkan::getBackendType() const { return backend_type::vulkan; }

void phi::vk::BackendVulkan::flushGPU() { vkDeviceWaitIdle(mDevice.getDevice()); }

void phi::vk::BackendVulkan::createDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                                 | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                             | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = detail::debug_callback;
    createInfo.pUserData = this;
    PHI_VK_VERIFY_SUCCESS(vkCreateDebugUtilsMessengerEXT(mInstance, &createInfo, nullptr, &mDebugMessenger));
}

phi::vk::BackendVulkan::per_thread_component& phi::vk::BackendVulkan::getCurrentThreadComponent()
{
    auto const current_index = mThreadAssociation.get_current_index();
    CC_ASSERT_MSG(current_index < mNumThreadComponents,
                  "Accessed phi Backend from more OS threads than configured in backend_config\n"
                  "recordCommandList() and submit() must only be used from at most backend_config::num_threads unique OS threads in total");
    return mThreadComponents[current_index];
}

cc::allocator* phi::vk::BackendVulkan::getCurrentScratchAlloc() { return &getCurrentThreadComponent().threadLocalScratchAlloc; }

void phi::vk::BackendVulkan::resetCurrentScratchAlloc() { getCurrentThreadComponent().threadLocalScratchAlloc.reset(); }
