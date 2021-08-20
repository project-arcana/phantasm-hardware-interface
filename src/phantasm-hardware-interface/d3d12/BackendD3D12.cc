#include "BackendD3D12.hh"

#ifdef PHI_HAS_SDL2
#include <SDL2/SDL_syswm.h>
#endif

#ifdef PHI_HAS_OPTICK
#include <optick/optick.h>
#endif

#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/window_handle.hh>

#include <rich-log/logger.hh>

#include "cmd_list_translation.hh"
#include "common/dxgi_format.hh"
#include "common/native_enum.hh"
#include "common/util.hh"
#include "common/verify.hh"

namespace phi::d3d12
{
struct BackendD3D12::per_thread_component
{
    command_list_translator translator;
    CommandAllocatorsPerThread cmd_list_allocator;
};
} // namespace phi::d3d12

void phi::d3d12::BackendD3D12::initialize(const phi::backend_config& config)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT();
#endif

    // enable colors as rich-log is used by this library
    rlog::enable_win32_colors();

    mFlushEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
    CC_ASSERT(mFlushEvent != INVALID_HANDLE_VALUE && "failed to create win32 event");

    CC_CONTRACT(config.static_allocator);
    CC_CONTRACT(config.dynamic_allocator);
    mDynamicAllocator = config.dynamic_allocator;

    // Core components
    {
        ID3D12Device* createdDevice = nullptr;
        mAdapter.initialize(config, createdDevice);
        mDevice.initialize(createdDevice, mAdapter.getAdapter(), config);
    }

    auto* const device = mDevice.getDevice();

    // queues
    {
#ifdef PHI_HAS_OPTICK
        OPTICK_EVENT("Queues");
#endif

        mDirectQueue.initialize(device, queue_type::direct);
        mComputeQueue.initialize(device, queue_type::compute);
        mCopyQueue.initialize(device, queue_type::copy);
    }

    // Global pools
    {
#ifdef PHI_HAS_OPTICK
        OPTICK_EVENT("Pools");
#endif

        mPoolResources.initialize(device, config.max_num_resources, config.max_num_swapchains, config.static_allocator, config.dynamic_allocator);
        mPoolShaderViews.initialize(device, &mPoolResources, &mPoolAccelStructs, config.max_num_shader_views,
                                    config.max_num_srvs + config.max_num_uavs, config.max_num_samplers, config.static_allocator);
        mPoolPSOs.initialize(device, config.max_num_pipeline_states, config.max_num_raytrace_pipeline_states, config.static_allocator, config.dynamic_allocator);
        mPoolFences.initialize(device, config.max_num_fences, config.static_allocator);
        mPoolQueries.initialize(device, config.num_timestamp_queries, config.num_occlusion_queries, config.num_pipeline_stat_queries, config.static_allocator);

        if (isRaytracingEnabled())
        {
            mPoolAccelStructs.initialize(device, &mPoolResources, config.max_num_accel_structs, config.static_allocator, config.dynamic_allocator);
            mShaderTableCtor.initialize(device, &mPoolShaderViews, &mPoolResources, &mPoolPSOs, &mPoolAccelStructs);
        }

        mPoolSwapchains.initialize(&mAdapter.getFactory(), device, mDirectQueue.command_queue, config.max_num_swapchains, config.static_allocator);
    }

    // Per-thread components and command list pool
    {
#ifdef PHI_HAS_OPTICK
        OPTICK_EVENT("TLS, Command List Pool");
#endif

        mThreadAssociation.initialize();

        mThreadComponentAlloc = config.static_allocator;
        mThreadComponents = config.static_allocator->new_array_sized<per_thread_component>(config.num_threads);
        mNumThreadComponents = config.num_threads;

        cc::alloc_array<CommandAllocatorsPerThread*> thread_allocator_ptrs(mNumThreadComponents, config.dynamic_allocator);

        for (auto i = 0u; i < mNumThreadComponents; ++i)
        {
            auto& thread_comp = mThreadComponents[i];
            thread_comp.translator.initialize(device, &mPoolShaderViews, &mPoolResources, &mPoolPSOs, &mPoolAccelStructs, &mPoolQueries);
            thread_allocator_ptrs[i] = &thread_comp.cmd_list_allocator;
        }

        mPoolCmdLists.initialize(*this, config.static_allocator,                                                                        //
                                 int(config.num_direct_cmdlist_allocators_per_thread), int(config.num_direct_cmdlists_per_allocator),   //
                                 int(config.num_compute_cmdlist_allocators_per_thread), int(config.num_compute_cmdlists_per_allocator), //
                                 int(config.num_copy_cmdlist_allocators_per_thread), int(config.num_copy_cmdlists_per_allocator),
                                 config.max_num_unique_transitions_per_cmdlist, //
                                 thread_allocator_ptrs);
    }

    mDiagnostics.init();

#ifdef PHI_HAS_OPTICK
    {
        ID3D12Device* device = nativeGetDevice();

        // for some reason optick interprets the amount of cmd queues as the device node count (device->getNodeCount())
        // thus only use the direct queue here
        ID3D12CommandQueue* cmdQueues[] = {nativeGetDirectQueue()};

        OPTICK_GPU_INIT_D3D12(device, cmdQueues, CC_COUNTOF(cmdQueues));
    }
#endif
}

void phi::d3d12::BackendD3D12::destroy()
{
    if (mAdapter.isValid())
    {
        flushGPU();

        mDiagnostics.free();

        //        mSwapchain.setFullscreen(false);
        mPoolSwapchains.destroy();

        mPoolCmdLists.destroy();
        mPoolAccelStructs.destroy();

        mPoolFences.destroy();
        mPoolPSOs.destroy();
        mPoolShaderViews.destroy();
        mPoolResources.destroy();
        mPoolQueries.destroy();

        for (auto i = 0u; i < mNumThreadComponents; ++i)
        {
            auto& thread_comp = mThreadComponents[i];
            thread_comp.cmd_list_allocator.destroy();
            thread_comp.translator.destroy();
        }
        reinterpret_cast<cc::allocator*>(mThreadComponentAlloc)->delete_array_sized(mThreadComponents, mNumThreadComponents);

        mDirectQueue.destroy();
        mCopyQueue.destroy();
        mComputeQueue.destroy();

        mDevice.destroy();
        mAdapter.destroy();

        ::CloseHandle(mFlushEvent);

        mThreadAssociation.destroy();
    }
}

phi::d3d12::BackendD3D12::~BackendD3D12() { destroy(); }

void phi::d3d12::BackendD3D12::flushGPU()
{
    auto lg = std::lock_guard(mFlushMutex);

    ++mFlushSignalVal;

    PHI_D3D12_VERIFY(mDirectQueue.command_queue->Signal(mDirectQueue.fence, mFlushSignalVal));
    PHI_D3D12_VERIFY(mComputeQueue.command_queue->Signal(mComputeQueue.fence, mFlushSignalVal));
    PHI_D3D12_VERIFY(mCopyQueue.command_queue->Signal(mCopyQueue.fence, mFlushSignalVal));

    ID3D12Fence* fences[] = {mDirectQueue.fence, mComputeQueue.fence, mCopyQueue.fence};
    uint64_t fence_vals[] = {mFlushSignalVal, mFlushSignalVal, mFlushSignalVal};

    PHI_D3D12_VERIFY(mDevice.getDevice()->SetEventOnMultipleFenceCompletion(fences, fence_vals, 3, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, mFlushEvent));
    ::WaitForSingleObject(mFlushEvent, INFINITE);
}

phi::handle::swapchain phi::d3d12::BackendD3D12::createSwapchain(const phi::window_handle& window_handle, tg::isize2 initial_size, phi::present_mode mode, uint32_t num_backbuffers)
{
    ::HWND native_hwnd = nullptr;
    {
        if (window_handle.type == window_handle::wh_win32_hwnd)
        {
            native_hwnd = window_handle.value.win32_hwnd;
        }
        else if (window_handle.type == window_handle::wh_sdl)
        {
#ifdef PHI_HAS_SDL2
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version)
            SDL_GetWindowWMInfo(window_handle.value.sdl_handle, &wmInfo);
            native_hwnd = wmInfo.info.win.window;
#else
            CC_RUNTIME_ASSERT(false && "SDL handle given, but compiled without SDL present");
#endif
        }
        else
        {
            CC_RUNTIME_ASSERT(false && "unimplemented window handle type");
        }
    }

    return mPoolSwapchains.createSwapchain(native_hwnd, initial_size.width, initial_size.height, num_backbuffers, mode);
}

void phi::d3d12::BackendD3D12::free(phi::handle::swapchain sc) { mPoolSwapchains.free(sc); }

phi::handle::resource phi::d3d12::BackendD3D12::acquireBackbuffer(handle::swapchain sc)
{
    auto const swapchain_index = mPoolSwapchains.getSwapchainIndex(sc);
    auto const backbuffer_i = mPoolSwapchains.acquireBackbuffer(sc);
    auto const& backbuffer = mPoolSwapchains.get(sc).backbuffers[backbuffer_i];
    return mPoolResources.injectBackbufferResource(swapchain_index, getBackbufferSize(sc), backbuffer.resource, backbuffer.state);
}

void phi::d3d12::BackendD3D12::present(phi::handle::swapchain sc) { mPoolSwapchains.present(sc); }

void phi::d3d12::BackendD3D12::onResize(handle::swapchain sc, tg::isize2 size)
{
    flushGPU();
    mPoolSwapchains.onResize(sc, size.width, size.height);
}

phi::format phi::d3d12::BackendD3D12::getBackbufferFormat(handle::swapchain /*sc*/) const
{
    return util::to_pr_format(mPoolSwapchains.getBackbufferFormat());
}

phi::handle::resource phi::d3d12::BackendD3D12::createTexture(arg::texture_description const& desc, char const* debug_name)
{
    return mPoolResources.createTexture(desc, debug_name);
}

phi::handle::resource phi::d3d12::BackendD3D12::createBuffer(arg::buffer_description const& desc, char const* debug_name)
{
    return mPoolResources.createBuffer(desc, debug_name);
}

std::byte* phi::d3d12::BackendD3D12::mapBuffer(phi::handle::resource res, int begin, int end) { return mPoolResources.mapBuffer(res, begin, end); }

void phi::d3d12::BackendD3D12::unmapBuffer(phi::handle::resource res, int begin, int end) { return mPoolResources.unmapBuffer(res, begin, end); }

void phi::d3d12::BackendD3D12::free(phi::handle::resource res) { mPoolResources.free(res); }

void phi::d3d12::BackendD3D12::freeRange(cc::span<const phi::handle::resource> resources) { mPoolResources.free(resources); }

phi::handle::shader_view phi::d3d12::BackendD3D12::createShaderView(cc::span<const phi::resource_view> srvs,
                                                                    cc::span<const phi::resource_view> uavs,
                                                                    cc::span<const phi::sampler_config> samplers,
                                                                    bool /*usage_compute*/)
{
    return mPoolShaderViews.create(srvs, uavs, samplers);
}

phi::handle::shader_view phi::d3d12::BackendD3D12::createEmptyShaderView(arg::shader_view_description const& desc, bool /*usage_compute*/)
{
    return mPoolShaderViews.createEmpty(desc.num_srvs + desc.num_uavs, desc.num_samplers);
}

void phi::d3d12::BackendD3D12::writeShaderViewSRVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> srvs)
{
    mPoolShaderViews.writeShaderViewSRVs(sv, offset, srvs);
}

void phi::d3d12::BackendD3D12::writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs)
{
    mPoolShaderViews.writeShaderViewUAVs(sv, offset, uavs);
}

void phi::d3d12::BackendD3D12::writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers)
{
    mPoolShaderViews.writeShaderViewSamplers(sv, offset, samplers);
}

void phi::d3d12::BackendD3D12::free(phi::handle::shader_view sv) { mPoolShaderViews.free(sv); }

void phi::d3d12::BackendD3D12::freeRange(cc::span<const phi::handle::shader_view> svs) { mPoolShaderViews.free(svs); }

phi::handle::pipeline_state phi::d3d12::BackendD3D12::createPipelineState(phi::arg::vertex_format vertex_format,
                                                                          const phi::arg::framebuffer_config& framebuffer_conf,
                                                                          phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                          bool has_root_constants,
                                                                          phi::arg::graphics_shaders shaders,
                                                                          const phi::pipeline_config& primitive_config,
                                                                          char const* debug_name)
{
    return mPoolPSOs.createPipelineState(vertex_format, framebuffer_conf, shader_arg_shapes, has_root_constants, shaders, primitive_config, debug_name);
}

phi::handle::pipeline_state phi::d3d12::BackendD3D12::createPipelineState(const phi::arg::graphics_pipeline_state_description& description, char const* debug_name)
{
    return mPoolPSOs.createPipelineState(description.vertices, description.framebuffer, description.shader_arg_shapes, description.has_root_constants,
                                         description.shader_binaries, description.config, debug_name);
}

phi::handle::pipeline_state phi::d3d12::BackendD3D12::createComputePipelineState(phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                                 phi::arg::shader_binary shader,
                                                                                 bool has_root_constants,
                                                                                 char const* debug_name)
{
    return mPoolPSOs.createComputePipelineState(shader_arg_shapes, shader, has_root_constants, debug_name);
}

phi::handle::pipeline_state phi::d3d12::BackendD3D12::createComputePipelineState(const phi::arg::compute_pipeline_state_description& description, char const* debug_name)
{
    return mPoolPSOs.createComputePipelineState(description.shader_arg_shapes, description.shader, description.has_root_constants, debug_name);
}

void phi::d3d12::BackendD3D12::free(phi::handle::pipeline_state ps) { mPoolPSOs.free(ps); }

phi::handle::command_list phi::d3d12::BackendD3D12::recordCommandList(std::byte const* buffer, size_t size, queue_type queue)
{
    auto& thread_comp = getCurrentThreadComponent();
    ID3D12GraphicsCommandList5* raw_list5;
    auto const res = mPoolCmdLists.create(raw_list5, thread_comp.cmd_list_allocator, queue);
    thread_comp.translator.translateCommandList(raw_list5, queue, mPoolCmdLists.getStateCache(res), buffer, size);
    return res;
}

void phi::d3d12::BackendD3D12::discard(cc::span<const phi::handle::command_list> cls) { mPoolCmdLists.freeOnDiscard(cls); }

void phi::d3d12::BackendD3D12::submit(cc::span<const phi::handle::command_list> cls,
                                      queue_type queue,
                                      cc::span<const fence_operation> fence_waits_before,
                                      cc::span<const fence_operation> fence_signals_after)
{
    constexpr uint32_t c_max_num_command_lists = 32u;
    cc::capped_vector<ID3D12CommandList*, c_max_num_command_lists * 2> cmd_bufs_to_submit;
    cc::capped_vector<handle::command_list, c_max_num_command_lists> barrier_lists;
    CC_ASSERT(cls.size() <= c_max_num_command_lists && "too many commandlists submitted at once");

    auto& thread_comp = getCurrentThreadComponent();


    for (auto const cl : cls)
    {
        if (cl == handle::null_command_list)
            continue;

        auto const* const state_cache = mPoolCmdLists.getStateCache(cl);

        cc::alloc_array<D3D12_RESOURCE_BARRIER> barriers_heap;
        D3D12_RESOURCE_BARRIER barriers_sbo[32];

        constexpr auto barrierSize = sizeof(D3D12_RESOURCE_BARRIER);

        uint32_t numBarriers = 0;
        D3D12_RESOURCE_BARRIER* barrierPtr = barriers_sbo;

        if (state_cache->num_entries > CC_COUNTOF(barriers_sbo))
        {
            barriers_heap.reset(mDynamicAllocator, state_cache->num_entries);
            barrierPtr = barriers_heap.data();
        }

        auto f_addBarrier = [&](D3D12_RESOURCE_BARRIER const& barrier) -> void { barrierPtr[numBarriers++] = barrier; };

        for (auto i = 0u; i < state_cache->num_entries; ++i)
        {
            auto const& entry = state_cache->entries[i];

            D3D12_RESOURCE_STATES const master_before = mPoolResources.getResourceState(entry.ptr);

            if (master_before != entry.required_initial)
            {
                // transition to the state required as the initial one
                f_addBarrier(util::get_barrier_desc(mPoolResources.getRawResource(entry.ptr), master_before, entry.required_initial));
            }

            // set the master state to the one in which this resource is left
            mPoolResources.setResourceState(entry.ptr, entry.current);
        }

        if (numBarriers > 0)
        {
            ID3D12GraphicsCommandList5* inserted_barrier_cmdlist;
            barrier_lists.push_back(mPoolCmdLists.create(inserted_barrier_cmdlist, thread_comp.cmd_list_allocator, queue));
            inserted_barrier_cmdlist->ResourceBarrier(numBarriers, barrierPtr);
            inserted_barrier_cmdlist->Close();
            cmd_bufs_to_submit.push_back(inserted_barrier_cmdlist);
        }

        cmd_bufs_to_submit.push_back(mPoolCmdLists.getRawList(cl));
    }


    ID3D12CommandQueue* const target_queue = getQueueByType(queue);

    for (auto const& wait_op : fence_waits_before)
    {
        mPoolFences.waitGPU(wait_op.fence, wait_op.value, target_queue);
    }

    target_queue->ExecuteCommandLists(UINT(cmd_bufs_to_submit.size()), cmd_bufs_to_submit.data());

    for (auto const& signal_op : fence_signals_after)
    {
        mPoolFences.signalGPU(signal_op.fence, signal_op.value, target_queue);
    }

    mPoolCmdLists.freeOnSubmit(barrier_lists, *target_queue);
    mPoolCmdLists.freeOnSubmit(cls, *target_queue);
}

phi::handle::fence phi::d3d12::BackendD3D12::createFence() { return mPoolFences.createFence(); }

uint64_t phi::d3d12::BackendD3D12::getFenceValue(phi::handle::fence fence) { return mPoolFences.getValue(fence); }

void phi::d3d12::BackendD3D12::signalFenceCPU(phi::handle::fence fence, uint64_t new_value) { mPoolFences.signalCPU(fence, new_value); }

void phi::d3d12::BackendD3D12::waitFenceCPU(phi::handle::fence fence, uint64_t wait_value) { mPoolFences.waitCPU(fence, wait_value); }

void phi::d3d12::BackendD3D12::signalFenceGPU(phi::handle::fence fence, uint64_t new_value, phi::queue_type queue)
{
    mPoolFences.signalGPU(fence, new_value, getQueueByType(queue));
}

void phi::d3d12::BackendD3D12::waitFenceGPU(phi::handle::fence fence, uint64_t wait_value, phi::queue_type queue)
{
    mPoolFences.waitGPU(fence, wait_value, getQueueByType(queue));
}

void phi::d3d12::BackendD3D12::free(cc::span<const phi::handle::fence> fences) { mPoolFences.free(fences); }

phi::handle::query_range phi::d3d12::BackendD3D12::createQueryRange(phi::query_type type, uint32_t size) { return mPoolQueries.create(type, size); }

void phi::d3d12::BackendD3D12::free(phi::handle::query_range query_range) { mPoolQueries.free(query_range); }

phi::handle::pipeline_state phi::d3d12::BackendD3D12::createRaytracingPipelineState(const arg::raytracing_pipeline_state_description& description, char const* debug_name)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    return mPoolPSOs.createRaytracingPipelineState(description.libraries, description.argument_associations, description.hit_groups, description.max_recursion,
                                                   description.max_payload_size_bytes, description.max_attribute_size_bytes, mDynamicAllocator, debug_name);
}

phi::handle::accel_struct phi::d3d12::BackendD3D12::createTopLevelAccelStruct(uint32_t num_instances, accel_struct_build_flags_t flags, accel_struct_prebuild_info* out_prebuild_info)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    return mPoolAccelStructs.createTopLevelAS(num_instances, flags, out_prebuild_info);
}

phi::handle::accel_struct phi::d3d12::BackendD3D12::createBottomLevelAccelStruct(cc::span<const phi::arg::blas_element> elements,
                                                                                 accel_struct_build_flags_t flags,
                                                                                 uint64_t* out_native_handle,
                                                                                 accel_struct_prebuild_info* out_prebuild_info)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    auto const res = mPoolAccelStructs.createBottomLevelAS(elements, flags, out_prebuild_info);

    if (out_native_handle != nullptr)
        *out_native_handle = mPoolAccelStructs.getNode(res).buffer_as_va;

    return res;
}

uint64_t phi::d3d12::BackendD3D12::getAccelStructNativeHandle(phi::handle::accel_struct as) { return mPoolAccelStructs.getNode(as).buffer_as_va; }

phi::shader_table_strides phi::d3d12::BackendD3D12::calculateShaderTableStrides(arg::shader_table_record const& ray_gen_record,
                                                                                arg::shader_table_records miss_records,
                                                                                arg::shader_table_records hit_group_records,
                                                                                arg::shader_table_records callable_records)
{
    return mShaderTableCtor.calculateShaderTableSizes(ray_gen_record, miss_records, hit_group_records, callable_records);
}

void phi::d3d12::BackendD3D12::writeShaderTable(std::byte* dest, handle::pipeline_state pso, uint32_t stride, arg::shader_table_records records)
{
    mShaderTableCtor.writeShaderTable(dest, pso, stride, records);
}

void phi::d3d12::BackendD3D12::free(phi::handle::accel_struct as)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    mPoolAccelStructs.free(as);
}

void phi::d3d12::BackendD3D12::freeRange(cc::span<const phi::handle::accel_struct> as)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    mPoolAccelStructs.free(as);
}

phi::arg::resource_description const& phi::d3d12::BackendD3D12::getResourceDescription(handle::resource res) const
{
    return mPoolResources.getResourceDescription(res);
}

phi::arg::texture_description const& phi::d3d12::BackendD3D12::getResourceTextureDescription(handle::resource res) const
{
    return mPoolResources.getTextureDescription(res);
}

phi::arg::buffer_description const& phi::d3d12::BackendD3D12::getResourceBufferDescription(handle::resource res) const
{
    return mPoolResources.getBufferDescription(res);
}

void phi::d3d12::BackendD3D12::setDebugName(phi::handle::resource res, cc::string_view name)
{
    mPoolResources.setDebugName(res, name.data(), uint32_t(name.length()));
}

bool phi::d3d12::BackendD3D12::startForcedDiagnosticCapture() { return mDiagnostics.start_capture(); }

bool phi::d3d12::BackendD3D12::endForcedDiagnosticCapture() { return mDiagnostics.end_capture(); }

uint64_t phi::d3d12::BackendD3D12::getGPUTimestampFrequency() const
{
    uint64_t res;
    mDirectQueue.command_queue->GetTimestampFrequency(&res);
    return res;
}

bool phi::d3d12::BackendD3D12::isRaytracingEnabled() const { return mDevice.hasRaytracing(); }

phi::vram_state_info phi::d3d12::BackendD3D12::nativeGetVRAMStateInfo()
{
    DXGI_QUERY_VIDEO_MEMORY_INFO nativeInfo = {};
    PHI_D3D12_VERIFY(mAdapter.getAdapter().QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &nativeInfo));

    vram_state_info res;
    res.os_budget_bytes = nativeInfo.Budget;
    res.current_usage_bytes = nativeInfo.CurrentUsage;
    res.available_for_reservation_bytes = nativeInfo.AvailableForReservation;
    res.current_reservation_bytes = nativeInfo.CurrentReservation;
    return res;
}

phi::d3d12::BackendD3D12::per_thread_component& phi::d3d12::BackendD3D12::getCurrentThreadComponent()
{
    auto const current_index = mThreadAssociation.get_current_index();
    CC_ASSERT_MSG(current_index < mNumThreadComponents,
                  "Accessed phi Backend from more OS threads than configured in backend_config\n"
                  "recordCommandList() and submit() must only be used from at most backend_config::num_threads unique OS threads in total");
    return mThreadComponents[current_index];
}
