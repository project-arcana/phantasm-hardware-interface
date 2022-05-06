#include "BackendVulkan.hh"

#ifdef PHI_HAS_OPTICK
#include <optick.h>
#endif

#include <clean-core/allocator.hh>
#include <clean-core/allocators/linear_allocator.hh>
#include <clean-core/defer.hh>
#include <clean-core/native/win32_util.hh>

#include <phantasm-hardware-interface/common/command_reading.hh>
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
    CommandAllocatorsPerThread cmdListAllocator;
    cc::alloc_array<std::byte> threadLocalScratchAllocMemory;
    cc::linear_allocator threadLocalScratchAlloc;
};
} // namespace phi::vk

phi::init_status phi::vk::BackendVulkan::initialize(const backend_config& config_arg)
{
    // initialize vulkan loader
    if (volkInitialize() != VK_SUCCESS)
    {
        PHI_LOG_ASSERT("Fatal: Failed to initialize Vulkan - vulkan-1.dll or libvulkan missing");
        return phi::init_status::err_runtime;
    }


    // copy explicitly for modifications
    backend_config config = config_arg;

    mDiagnostics.init();
    if (mDiagnostics.is_renderdoc_present() && config.validation >= validation_level::on)
    {
        PHI_LOG("Validation layers requested while running RenderDoc, disabling due to known crashes");
        config.validation = validation_level::off;
    }

    // initialize per-thread components and scratch allocs
    {
        mThreadAssociation.initialize();

        mThreadComponentAlloc = config.static_allocator;
        mThreadComponents = config.static_allocator->new_array_sized<per_thread_component>(config.num_threads);
        mNumThreadComponents = config.num_threads;

        for (auto i = 0u; i < mNumThreadComponents; ++i)
        {
            auto& thread_comp = mThreadComponents[i];
            // 5 MB scratch alloc per thread
            thread_comp.threadLocalScratchAllocMemory.reset(config.static_allocator, 1024 * 1024 * 5);
            thread_comp.threadLocalScratchAlloc = cc::linear_allocator(thread_comp.threadLocalScratchAllocMemory);
        }
    }

    {
        cc::allocator* scratch = getCurrentScratchAlloc();

        auto const active_lay_ext = getUsedInstanceExtensions(getAvailableInstanceExtensions(scratch), config);

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
        if (create_res != VK_SUCCESS)
        {
            PHI_LOG_ASSERT("Fatal: vkCreateInstance call failed");
            return init_status::err_runtime;
        }

        // TODO: More fine-grained error handling
        PHI_VK_ASSERT_SUCCESS(create_res);
    }

    // Load instance-based Vulkan entrypoints
    volkLoadInstanceOnly(mInstance);

    if (config.validation != validation_level::off)
    {
        // Debug callback
        createDebugMessenger();
    }

    // GPU choice and device init
    {
        cc::allocator* scratch = getCurrentScratchAlloc();

        auto const vk_gpu_infos = get_all_vulkan_gpu_infos(mInstance, scratch);
        auto const gpu_infos = get_available_gpus(vk_gpu_infos, scratch);
        auto const chosen_index = getPreferredGPU(gpu_infos, config.adapter);

        if (chosen_index >= gpu_infos.size())
        {
            PHI_LOG_ASSERT("Fatal: Failed to find an eligble GPU");
            return init_status::err_no_gpu_eligible;
        }

        auto const& chosen_gpu = gpu_infos[chosen_index];
        auto const& chosen_vk_gpu = vk_gpu_infos[chosen_gpu.index];

        if (!mDevice.initialize(chosen_vk_gpu, config))
        {
            PHI_LOG_ASSERT("Failed to intialize on GPU {}", gpu_infos[chosen_index].name);

#ifdef CC_OS_LINUX
            if (config.enable_raytracing && chosen_gpu.vendor == gpu_vendor::nvidia)
            {
                // Potentially preceded by vulkan warnings
                // "terminator_CreateDevice: Failed in ICD libGLX_nvidia.so.0 vkCreateDevicecall"
                // and "vkCreateDevice:  Failed to create device chain."
                //
                // I got this issue on Nvidia drivers 470.103.01, Debian, RTX 2080, April 22
                // Only happens when requesting the VK_NV_ray_tracing device extension - no idea about resolution though
                PHI_LOG_ASSERT("If you got the message \"terminator_CreateDevice: Failed in ICD libGLX_nvidia.so.0 vkCreateDevicecall\":");
                PHI_LOG_ASSERT("    Known issue - try disabling raytracing in the PHI backend config");
            }
#endif

            return init_status::err_runtime;
        }

        printStartupMessage(gpu_infos.size(), &gpu_infos[chosen_index], config, false);

        if (config.print_startup_message)
        {
            PHI_LOG("   compiled with vulkan sdk v{}.{}.{}", vkver::major, vkver::minor, vkver::patch);
        }

        mGPUInfo = gpu_infos[chosen_index];
    }

    // Pool init
    mPoolPipelines.initialize(mDevice.getDevice(), config.max_num_pipeline_states, config.static_allocator);
    mPoolResources.initialize(mDevice.getPhysicalDevice(), mDevice.getDevice(), config.max_num_resources, config.max_num_swapchains, config.static_allocator);
    mPoolShaderViews.initialize(mDevice.getDevice(), &mPoolResources, &mPoolAccelStructs, config.max_num_shader_views, config.max_num_srvs,
                                config.max_num_uavs, config.max_num_samplers, config.static_allocator);
    mPoolFences.initialize(mDevice.getDevice(), config.max_num_fences, config.static_allocator);
    mPoolQueries.initialize(mDevice.getDevice(), config.num_timestamp_queries, config.num_occlusion_queries, config.num_pipeline_stat_queries, config.static_allocator);

    if (isRaytracingEnabled())
    {
        mPoolAccelStructs.initialize(mDevice.getDevice(), &mPoolResources, config.max_num_accel_structs, config.static_allocator);
        mShaderTableCtor.initialize(mDevice.getDevice(), &mPoolShaderViews, &mPoolResources, &mPoolPipelines, &mPoolAccelStructs);
    }

    mPoolSwapchains.initialize(mInstance, mDevice, config);

    // Per-thread command list pool
    {
        cc::allocator* scratch = getCurrentScratchAlloc();
        cc::alloc_array<CommandAllocatorsPerThread*> thread_allocator_ptrs(mNumThreadComponents, scratch);

        for (auto i = 0u; i < mNumThreadComponents; ++i)
        {
            auto& thread_comp = mThreadComponents[i];
            thread_allocator_ptrs[i] = &thread_comp.cmdListAllocator;
        }

        // TODO: config struct
        mPoolTranslators.initialize(mDevice.getDevice(), &mPoolShaderViews, &mPoolResources, &mPoolPipelines, &mPoolCmdLists, &mPoolQueries,
                                    &mPoolAccelStructs, mDevice.hasRaytracing(), config.static_allocator, config.max_num_live_commandlists);

        // TODO: config struct
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

    return init_status::success;
}

void phi::vk::BackendVulkan::destroy()
{
    if (mInstance == nullptr)
    {
        // never initialized or immediately failed
        return;
    }

    if (mDevice.getDevice() != nullptr)
    {
        // only shut these components down if the device was initialized
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
        mPoolTranslators.destroy();

        for (auto i = 0u; i < mNumThreadComponents; ++i)
        {
            auto& thread_comp = mThreadComponents[i];
            thread_comp.cmdListAllocator.destroy(mDevice.getDevice());
            thread_comp.threadLocalScratchAllocMemory = {};
        }
        static_cast<cc::allocator*>(mThreadComponentAlloc)->delete_array_sized(mThreadComponents, mNumThreadComponents);

        mDevice.destroy();
    }

    if (mDebugMessenger != nullptr)
        vkDestroyDebugUtilsMessengerEXT(mInstance, mDebugMessenger, nullptr);

    vkDestroyInstance(mInstance, nullptr);
    mInstance = nullptr;

    mThreadAssociation.destroy();
}

phi::vk::BackendVulkan::~BackendVulkan() { destroy(); }

phi::handle::swapchain phi::vk::BackendVulkan::createSwapchain(const phi::window_handle& window_handle, tg::isize2 initial_size, phi::present_mode mode, uint32_t num_backbuffers)
{
    auto const res = mPoolSwapchains.createSwapchain(window_handle, initial_size.width, initial_size.height, num_backbuffers, mode, getCurrentScratchAlloc());
    return res;
}

void phi::vk::BackendVulkan::free(phi::handle::swapchain sc) { mPoolSwapchains.free(sc); }

phi::handle::resource phi::vk::BackendVulkan::acquireBackbuffer(handle::swapchain sc)
{
    auto const swapchain_index = mPoolSwapchains.getSwapchainIndex(sc);
    auto const& swapchain = mPoolSwapchains.get(sc);
    auto const prev_backbuffer_index = swapchain.active_image_index;
    bool const acquire_success = mPoolSwapchains.acquireBackbuffer(sc, getCurrentScratchAlloc());

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

void phi::vk::BackendVulkan::present(phi::handle::swapchain sc) { mPoolSwapchains.present(sc, getCurrentScratchAlloc()); }

void phi::vk::BackendVulkan::onResize(handle::swapchain sc, tg::isize2 size)
{
    flushGPU();
    mPoolSwapchains.onResize(sc, size.width, size.height, getCurrentScratchAlloc());
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
}

void phi::vk::BackendVulkan::writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs)
{
    mPoolShaderViews.writeShaderViewUAVs(sv, offset, uavs, getCurrentScratchAlloc());
}

void phi::vk::BackendVulkan::writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers)
{
    mPoolShaderViews.writeShaderViewSamplers(sv, offset, samplers, getCurrentScratchAlloc());
}

void phi::vk::BackendVulkan::free(phi::handle::shader_view sv) { mPoolShaderViews.free(sv); }

void phi::vk::BackendVulkan::freeRange(cc::span<const phi::handle::shader_view> svs) { mPoolShaderViews.free(svs); }

phi::handle::pipeline_state phi::vk::BackendVulkan::createPipelineState(const phi::arg::graphics_pipeline_state_description& description, char const* debug_name)
{
    auto const res = mPoolPipelines.createPipelineState(description.vertices, description.framebuffer, description.root_signature.shader_arg_shapes,
                                                        description.root_signature.has_root_constants, description.shader_binaries,
                                                        description.config, getCurrentScratchAlloc(), debug_name);
    return res;
}

phi::handle::pipeline_state phi::vk::BackendVulkan::createComputePipelineState(const phi::arg::compute_pipeline_state_description& description, char const* debug_name)
{
    auto const res = mPoolPipelines.createComputePipelineState(description.root_signature.shader_arg_shapes, description.shader,
                                                               description.root_signature.has_root_constants, getCurrentScratchAlloc(), debug_name);
    return res;
}

void phi::vk::BackendVulkan::free(phi::handle::pipeline_state ps) { mPoolPipelines.free(ps); }

phi::handle::command_list phi::vk::BackendVulkan::recordCommandList(std::byte const* buffer, size_t size, queue_type queue)
{
    command_stream_parser parser(buffer, size);
    command_stream_parser::iterator parserIterator = parser.begin();

    cmd::set_global_profile_scope const* cmdGlobalProfile = nullptr;
    if (parserIterator.has_cmds_left() && parserIterator.get_current_cmd_type() == phi::cmd::detail::cmd_type::set_global_profile_scope)
    {
        // if the very first command is set_global_profile_scope, use the provided event instead of the static one
        cmdGlobalProfile = static_cast<cmd::set_global_profile_scope const*>(parserIterator.get_current_cmd());
        parserIterator.skip_one_cmd();
    }

    auto const liveCmdlist = openLiveCommandList(queue, cmdGlobalProfile);

    auto* const pTranslator = mPoolTranslators.getTranslator(liveCmdlist);

    // translate all contained commands
    while (parserIterator.has_cmds_left())
    {
        cmd::detail::dynamic_dispatch(*parserIterator.get_current_cmd(), *pTranslator);
        parserIterator.skip_one_cmd();
    }

    return closeLiveCommandList(liveCmdlist);
}

void phi::vk::BackendVulkan::discard(cc::span<const phi::handle::command_list> cls) { mPoolCmdLists.freeAndDiscard(cls); }

void phi::vk::BackendVulkan::submit(cc::span<const phi::handle::command_list> cls,
                                    phi::queue_type queue,
                                    cc::span<const phi::fence_operation> fence_waits_before,
                                    cc::span<const phi::fence_operation> fence_signals_after)
{
    auto* scratch = getCurrentScratchAlloc();

    cc::alloc_vector<VkCommandBuffer> cmd_bufs_to_submit;
    cmd_bufs_to_submit.reset_reserve(scratch, cls.size() * 2);

    cc::alloc_vector<handle::command_list> barrier_lists;
    barrier_lists.reset_reserve(scratch, cls.size());

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

    VkPipelineStageFlags const arrWaitDestMasks[8]
        = {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
    submit_info.pWaitDstStageMask = arrWaitDestMasks;
    // signal semaphores
    submit_info.signalSemaphoreCount = uint32_t(fence_signals_after.size());
    submit_info.pSignalSemaphores = signal_semaphores;


    VkQueue const submit_queue = mDevice.getRawQueue(queue);
    VkFence submit_fence;
    auto const submit_fence_index = mPoolCmdLists.acquireFence(submit_fence);
    PHI_VK_VERIFY_SUCCESS(vkQueueSubmit(submit_queue, 1, &submit_info, submit_fence));

    cc::span<handle::command_list const> submit_spans[] = {barrier_lists, cls};
    mPoolCmdLists.freeOnSubmit(submit_spans, submit_fence_index);
}

phi::handle::fence phi::vk::BackendVulkan::createFence() { return mPoolFences.createFence(); }

uint64_t phi::vk::BackendVulkan::getFenceValue(phi::handle::fence fence) { return mPoolFences.getValue(fence); }

void phi::vk::BackendVulkan::signalFenceCPU(phi::handle::fence fence, uint64_t new_value) { mPoolFences.signalCPU(fence, new_value); }

void phi::vk::BackendVulkan::waitFenceCPU(phi::handle::fence fence, uint64_t wait_value) { mPoolFences.waitCPU(fence, wait_value); }

void phi::vk::BackendVulkan::free(cc::span<const phi::handle::fence> fences) { mPoolFences.free(fences); }

phi::handle::query_range phi::vk::BackendVulkan::createQueryRange(phi::query_type type, uint32_t size) { return mPoolQueries.create(type, size); }

void phi::vk::BackendVulkan::free(phi::handle::query_range query_range) { mPoolQueries.free(query_range); }

phi::handle::pipeline_state phi::vk::BackendVulkan::createRaytracingPipelineState(const arg::raytracing_pipeline_state_description& description, char const* debug_name)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");

    (void)debug_name; // TODO
    auto const res = mPoolPipelines.createRaytracingPipelineState(description.libraries, description.argument_associations, description.hit_groups,
                                                                  description.max_recursion, description.max_payload_size_bytes,
                                                                  description.max_attribute_size_bytes, getCurrentScratchAlloc());
    return res;
}

phi::handle::accel_struct phi::vk::BackendVulkan::createTopLevelAccelStruct(uint32_t num_instances, accel_struct_build_flags_t flags, accel_struct_prebuild_info* out_prebuild_info)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    (void)out_prebuild_info; // TODO
    (void)flags;             // TODO
    return mPoolAccelStructs.createTopLevelAS(num_instances);
}

phi::handle::accel_struct phi::vk::BackendVulkan::createBottomLevelAccelStruct(cc::span<const phi::arg::blas_element> elements,
                                                                               accel_struct_build_flags_t flags,
                                                                               uint64_t* out_native_handle,
                                                                               accel_struct_prebuild_info* out_prebuild_info)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    auto const res = mPoolAccelStructs.createBottomLevelAS(elements, flags);

    (void)out_prebuild_info; // TODO

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

phi::handle::live_command_list phi::vk::BackendVulkan::openLiveCommandList(queue_type queue, const cmd::set_global_profile_scope* opt_global_pscope)
{
    // possibly fall back to a direct queue
    queue = mDevice.getQueueTypeOrFallback(queue);

    auto& thread_comp = getCurrentThreadComponent();

    VkCommandBuffer raw_list;
    auto const res = mPoolCmdLists.create(raw_list, thread_comp.cmdListAllocator, queue);

    return mPoolTranslators.createLiveCmdList(res, raw_list, queue, mPoolCmdLists.getStateCache(res), opt_global_pscope);
}

phi::handle::command_list phi::vk::BackendVulkan::closeLiveCommandList(handle::live_command_list list)
{
    //
    return mPoolTranslators.freeLiveCmdList(list, true);
}

void phi::vk::BackendVulkan::discardLiveCommandList(handle::live_command_list list)
{
    handle::command_list const backingList = mPoolTranslators.freeLiveCmdList(list, false);
    discard(cc::span{backingList});
}


void phi::vk::BackendVulkan::cmdDraw(handle::live_command_list list, cmd::draw const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdDrawIndirect(handle::live_command_list list, cmd::draw_indirect const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdDispatch(handle::live_command_list list, cmd::dispatch const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdDispatchIndirect(handle::live_command_list list, cmd::dispatch_indirect const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdTransitionResources(handle::live_command_list list, cmd::transition_resources const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdBarrierUAV(handle::live_command_list list, cmd::barrier_uav const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdTransitionImageSlices(handle::live_command_list list, cmd::transition_image_slices const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdCopyBuffer(handle::live_command_list list, cmd::copy_buffer const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdCopyTexture(handle::live_command_list list, cmd::copy_texture const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdCopyBufferToTexture(handle::live_command_list list, cmd::copy_buffer_to_texture const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdCopyTextureToBuffer(handle::live_command_list list, cmd::copy_texture_to_buffer const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdResolveTexture(handle::live_command_list list, cmd::resolve_texture const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdBeginRenderPass(handle::live_command_list list, cmd::begin_render_pass const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdEndRenderPass(handle::live_command_list list, cmd::end_render_pass const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdWriteTimestamp(handle::live_command_list list, cmd::write_timestamp const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdResolveQueries(handle::live_command_list list, cmd::resolve_queries const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdBeginDebugLabel(handle::live_command_list list, cmd::begin_debug_label const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdEndDebugLabel(handle::live_command_list list, cmd::end_debug_label const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdUpdateBottomLevel(handle::live_command_list list, cmd::update_bottom_level const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdUpdateTopLevel(handle::live_command_list list, cmd::update_top_level const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdDispatchRays(handle::live_command_list list, cmd::dispatch_rays const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdClearTextures(handle::live_command_list list, cmd::clear_textures const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdBeginProfileScope(handle::live_command_list list, cmd::begin_profile_scope const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
}

void phi::vk::BackendVulkan::cmdEndProfileScope(handle::live_command_list list, cmd::end_profile_scope const& command)
{
    mPoolTranslators.getTranslator(list)->execute(command);
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
    auto const current_index = mThreadAssociation.getCurrentIndex();
    CC_ASSERT_MSG(current_index < (int)mNumThreadComponents,
                  "Accessed phi Backend from more OS threads than configured in backend_config\n"
                  "recordCommandList() and submit() must only be used from at most backend_config::num_threads unique OS threads in total");
    return mThreadComponents[current_index];
}

cc::allocator* phi::vk::BackendVulkan::getCurrentScratchAlloc()
{
    cc::linear_allocator* res = &getCurrentThreadComponent().threadLocalScratchAlloc;
    res->reset();
    return res;
}
