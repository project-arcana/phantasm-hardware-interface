#include "BackendD3D12.hh"

#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/window_handle.hh>

#include <rich-log/logger.hh>

#include "cmd_list_translation.hh"
#include "common/dxgi_format.hh"
#include "common/native_enum.hh"
#include "common/util.hh"
#include "common/verify.hh"

#ifdef PHI_HAS_SDL2
#include <SDL2/SDL_syswm.h>
#endif

namespace phi::d3d12
{
struct BackendD3D12::per_thread_component
{
    command_list_translator translator;
    CommandAllocatorsPerThread cmd_list_allocator;
};

}

void phi::d3d12::BackendD3D12::initialize(const phi::backend_config& config)
{
    // enable colors as rich-log is used by this library
    rlog::enable_win32_colors();

    mFlushEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
    CC_ASSERT(mFlushEvent != INVALID_HANDLE_VALUE && "failed to create win32 event");

    // Core components
    {
        mAdapter.initialize(config);
        mDevice.initialize(mAdapter.getAdapter(), config);
    }

    auto* const device = mDevice.getDevice();

    // queues
    {
        mDirectQueue.initialize(device, queue_type::direct);
        mComputeQueue.initialize(device, queue_type::compute);
        mCopyQueue.initialize(device, queue_type::copy);
    }

    // Global pools
    {
        mPoolResources.initialize(device, config.max_num_resources, config.max_num_swapchains, config.static_allocator, config.dynamic_allocator);
        mPoolShaderViews.initialize(device, &mPoolResources, config.max_num_cbvs, config.max_num_srvs + config.max_num_uavs, config.max_num_samplers,
                                    config.static_allocator);
        mPoolPSOs.initialize(device, config.max_num_pipeline_states, config.max_num_raytrace_pipeline_states, config.static_allocator, config.dynamic_allocator);
        mPoolFences.initialize(device, config.max_num_fences, config.static_allocator);
        mPoolQueries.initialize(device, config.num_timestamp_queries, config.num_occlusion_queries, config.num_pipeline_stat_queries, config.static_allocator);

        if (isRaytracingEnabled())
        {
            mPoolAccelStructs.initialize(device, &mPoolResources, config.max_num_accel_structs, config.static_allocator, config.dynamic_allocator);
            mShaderTableCtor.initialize(device, &mPoolShaderViews, &mPoolResources, &mPoolPSOs, &mPoolAccelStructs);
        }

        mPoolSwapchains.initialize(&mAdapter.getFactory(), device, config.present_from_compute_queue ? mComputeQueue.command_queue : mDirectQueue.command_queue,
                                   config.max_num_swapchains, config.static_allocator);
    }

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
            thread_comp.translator.initialize(device, &mPoolShaderViews, &mPoolResources, &mPoolPSOs, &mPoolAccelStructs, &mPoolQueries);
            thread_allocator_ptrs[i] = &thread_comp.cmd_list_allocator;
        }

        mPoolCmdLists.initialize(*this,                                                                                                 //
                                 int(config.num_direct_cmdlist_allocators_per_thread), int(config.num_direct_cmdlists_per_allocator),   //
                                 int(config.num_compute_cmdlist_allocators_per_thread), int(config.num_compute_cmdlists_per_allocator), //
                                 int(config.num_copy_cmdlist_allocators_per_thread), int(config.num_copy_cmdlists_per_allocator),       //
                                 thread_allocator_ptrs);
    }

    mDiagnostics.init();
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

phi::handle::swapchain phi::d3d12::BackendD3D12::createSwapchain(const phi::window_handle& window_handle, tg::isize2 initial_size, phi::present_mode mode, unsigned num_backbuffers)
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

phi::handle::resource phi::d3d12::BackendD3D12::acquireBackbuffer(handle::swapchain sc)
{
    auto const swapchain_index = mPoolSwapchains.getSwapchainIndex(sc);
    auto const backbuffer_i = mPoolSwapchains.waitForBackbuffer(sc);
    auto const& backbuffer = mPoolSwapchains.get(sc).backbuffers[backbuffer_i];
    return mPoolResources.injectBackbufferResource(swapchain_index, backbuffer.resource, backbuffer.state);
}

void phi::d3d12::BackendD3D12::onResize(handle::swapchain sc, tg::isize2 size)
{
    flushGPU();
    mPoolSwapchains.onResize(sc, size.width, size.height);
}

phi::format phi::d3d12::BackendD3D12::getBackbufferFormat(handle::swapchain /*sc*/) const
{
    return util::to_pr_format(mPoolSwapchains.getBackbufferFormat());
}

phi::handle::command_list phi::d3d12::BackendD3D12::recordCommandList(std::byte* buffer, size_t size, queue_type queue)
{
    auto& thread_comp = getCurrentThreadComponent();
    ID3D12GraphicsCommandList5* raw_list5;
    auto const res = mPoolCmdLists.create(raw_list5, thread_comp.cmd_list_allocator, queue);
    thread_comp.translator.translateCommandList(raw_list5, queue, mPoolCmdLists.getStateCache(res), buffer, size);
    return res;
}

void phi::d3d12::BackendD3D12::submit(cc::span<const phi::handle::command_list> cls,
                                      queue_type queue,
                                      cc::span<const fence_operation> fence_waits_before,
                                      cc::span<const fence_operation> fence_signals_after)
{
    constexpr unsigned c_max_num_command_lists = 32u;
    cc::capped_vector<ID3D12CommandList*, c_max_num_command_lists * 2> cmd_bufs_to_submit;
    cc::capped_vector<handle::command_list, c_max_num_command_lists> barrier_lists;
    CC_ASSERT(cls.size() <= c_max_num_command_lists && "too many commandlists submitted at once");

    auto& thread_comp = getCurrentThreadComponent();


    for (auto const cl : cls)
    {
        if (cl == handle::null_command_list)
            continue;

        auto const* const state_cache = mPoolCmdLists.getStateCache(cl);
        cc::capped_vector<D3D12_RESOURCE_BARRIER, 32> barriers;

        for (auto const& entry : state_cache->cache)
        {
            D3D12_RESOURCE_STATES const master_before = mPoolResources.getResourceState(entry.ptr);

            if (master_before != entry.required_initial)
            {
                // transition to the state required as the initial one
                barriers.push_back(util::get_barrier_desc(mPoolResources.getRawResource(entry.ptr), master_before, entry.required_initial));
            }

            // set the master state to the one in which this resource is left
            mPoolResources.setResourceState(entry.ptr, entry.current);
        }

        if (!barriers.empty())
        {
            ID3D12GraphicsCommandList5* inserted_barrier_cmdlist;
            barrier_lists.push_back(mPoolCmdLists.create(inserted_barrier_cmdlist, thread_comp.cmd_list_allocator, queue));
            inserted_barrier_cmdlist->ResourceBarrier(UINT(barriers.size()), barriers.data());
            inserted_barrier_cmdlist->Close();
            cmd_bufs_to_submit.push_back(inserted_barrier_cmdlist);
        }

        cmd_bufs_to_submit.push_back(mPoolCmdLists.getRawList(cl));
    }


    ID3D12CommandQueue& target_queue = getQueueByType(queue);

    for (auto const& wait_op : fence_waits_before)
    {
        mPoolFences.waitGPU(wait_op.fence, wait_op.value, target_queue);
    }

    target_queue.ExecuteCommandLists(UINT(cmd_bufs_to_submit.size()), cmd_bufs_to_submit.data());

    for (auto const& signal_op : fence_signals_after)
    {
        mPoolFences.signalGPU(signal_op.fence, signal_op.value, target_queue);
    }

    mPoolCmdLists.freeOnSubmit(barrier_lists, target_queue);
    mPoolCmdLists.freeOnSubmit(cls, target_queue);
}

phi::handle::pipeline_state phi::d3d12::BackendD3D12::createRaytracingPipelineState(arg::raytracing_shader_libraries libraries,
                                                                                    arg::raytracing_argument_associations arg_assocs,
                                                                                    arg::raytracing_hit_groups hit_groups,
                                                                                    unsigned max_recursion,
                                                                                    unsigned max_payload_size_bytes,
                                                                                    unsigned max_attribute_size_bytes)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    return mPoolPSOs.createRaytracingPipelineState(libraries, arg_assocs, hit_groups, max_recursion, max_payload_size_bytes, max_attribute_size_bytes,
                                                   cc::system_allocator);
}

phi::handle::accel_struct phi::d3d12::BackendD3D12::createTopLevelAccelStruct(unsigned num_instances, accel_struct_build_flags_t flags)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    return mPoolAccelStructs.createTopLevelAS(num_instances, flags);
}

phi::handle::accel_struct phi::d3d12::BackendD3D12::createBottomLevelAccelStruct(cc::span<const phi::arg::blas_element> elements,
                                                                                 accel_struct_build_flags_t flags,
                                                                                 uint64_t* out_native_handle)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    auto const res = mPoolAccelStructs.createBottomLevelAS(elements, flags);

    if (out_native_handle != nullptr)
        *out_native_handle = mPoolAccelStructs.getNode(res).raw_as_handle;

    return res;
}

phi::handle::resource phi::d3d12::BackendD3D12::getAccelStructBuffer(phi::handle::accel_struct as) { return mPoolAccelStructs.getNode(as).buffer_as; }

uint64_t phi::d3d12::BackendD3D12::getAccelStructNativeHandle(phi::handle::accel_struct as) { return mPoolAccelStructs.getNode(as).raw_as_handle; }

phi::shader_table_strides phi::d3d12::BackendD3D12::calculateShaderTableSize(arg::shader_table_record const& ray_gen_record,
                                                                           arg::shader_table_records miss_records,
                                                                           arg::shader_table_records hit_group_records,
                                                                           arg::shader_table_records callable_records)
{
    return mShaderTableCtor.calculateShaderTableSizes(ray_gen_record, miss_records, hit_group_records, callable_records);
}

void phi::d3d12::BackendD3D12::writeShaderTable(std::byte* dest, handle::pipeline_state pso, unsigned stride, arg::shader_table_records records)
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

void phi::d3d12::BackendD3D12::setDebugName(phi::handle::resource res, cc::string_view name)
{
    mPoolResources.setDebugName(res, name.data(), unsigned(name.length()));
}

void phi::d3d12::BackendD3D12::printInformation(phi::handle::resource res) const
{
    (void)res;
    PHI_LOG << "printInformation unimplemented";
}

bool phi::d3d12::BackendD3D12::startForcedDiagnosticCapture() { return mDiagnostics.start_capture(); }

bool phi::d3d12::BackendD3D12::endForcedDiagnosticCapture() { return mDiagnostics.end_capture(); }

bool phi::d3d12::BackendD3D12::isRaytracingEnabled() const { return mDevice.hasRaytracing(); }

phi::d3d12::BackendD3D12::per_thread_component& phi::d3d12::BackendD3D12::getCurrentThreadComponent()
{
    auto const current_index = mThreadAssociation.get_current_index();
    CC_ASSERT_MSG(current_index < mNumThreadComponents,
                  "Accessed phi Backend from more OS threads than configured in backend_config\n"
                  "recordCommandList() and submit() must only be used from at most backend_config::num_threads unique OS threads in total");
    return mThreadComponents[current_index];
}
