#include "BackendD3D12.hh"

#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/window_handle.hh>

#include "cmd_list_translation.hh"
#include "common/dxgi_format.hh"
#include "common/log.hh"
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

void phi::d3d12::BackendD3D12::initialize(const phi::backend_config& config, const window_handle& window_handle)
{
    mFlushEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
    CC_ASSERT(mFlushEvent != INVALID_HANDLE_VALUE && "failed to create win32 event");

    // Core components
    {
        mAdapter.initialize(config);
        mDevice.initialize(mAdapter.getAdapter(), config);

        mDirectQueue.initialize(mDevice.getDevice(), queue_type::direct);
        mComputeQueue.initialize(mDevice.getDevice(), queue_type::compute);
        mCopyQueue.initialize(mDevice.getDevice(), queue_type::copy);

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
                SDL_GetWindowWMInfo(cc::bit_cast<::SDL_Window*>(window_handle.value.sdl_handle), &wmInfo);
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

        mSwapchain.initialize(mAdapter.getFactory(), mDevice.getDeviceShared(),
                              config.present_from_compute_queue ? mComputeQueue.getQueueShared() : mDirectQueue.getQueueShared(), native_hwnd,
                              config.num_backbuffers, config.present);
    }

    auto& device = mDevice.getDevice();

    // Global pools
    {
        mPoolResources.initialize(device, config.max_num_resources);
        mPoolShaderViews.initialize(&device, &mPoolResources, config.max_num_cbvs, config.max_num_srvs + config.max_num_uavs, config.max_num_samplers);
        mPoolPSOs.initialize(mDevice.getDevice5(), config.max_num_pipeline_states, config.max_num_raytrace_pipeline_states);
        mPoolFences.initialize(&device, config.max_num_fences);

        if (isRaytracingEnabled())
        {
            mPoolAccelStructs.initialize(mDevice.getDevice5(), &mPoolResources, config.max_num_accel_structs);
            mShaderTableCtor.initialize(mDevice.getDevice5(), &mPoolShaderViews, &mPoolResources, &mPoolPSOs, &mPoolAccelStructs);
        }
    }

    // Per-thread components and command list pool
    {
        mThreadAssociation.initialize();
        mThreadComponents = mThreadComponents.defaulted(config.num_threads);

        cc::vector<CommandAllocatorsPerThread*> thread_allocator_ptrs;
        thread_allocator_ptrs.reserve(config.num_threads);

        for (auto& thread_comp : mThreadComponents)
        {
            thread_comp.translator.initialize(&device, &mPoolShaderViews, &mPoolResources, &mPoolPSOs, &mPoolAccelStructs);
            thread_allocator_ptrs.push_back(&thread_comp.cmd_list_allocator);
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

        mSwapchain.setFullscreen(false);

        mPoolCmdLists.destroy();
        mPoolAccelStructs.destroy();

        mPoolFences.destroy();
        mPoolPSOs.destroy();
        mPoolShaderViews.destroy();
        mPoolResources.destroy();

        for (auto& thread_comp : mThreadComponents)
        {
            thread_comp.cmd_list_allocator.destroy();
        }

        mAdapter.invalidate();

        ::CloseHandle(mFlushEvent);
    }
}

phi::d3d12::BackendD3D12::~BackendD3D12() { destroy(); }

void phi::d3d12::BackendD3D12::flushGPU()
{
    auto lg = std::lock_guard(mFlushMutex);

    ++mFlushSignalVal;

    PHI_D3D12_VERIFY(mDirectQueue.getQueue().Signal(&mDirectQueue.getFence(), mFlushSignalVal));
    PHI_D3D12_VERIFY(mComputeQueue.getQueue().Signal(&mComputeQueue.getFence(), mFlushSignalVal));
    PHI_D3D12_VERIFY(mCopyQueue.getQueue().Signal(&mCopyQueue.getFence(), mFlushSignalVal));

    cc::array<ID3D12Fence*, 3> fences;
    fences[0] = &mDirectQueue.getFence();
    fences[1] = &mComputeQueue.getFence();
    fences[2] = &mCopyQueue.getFence();

    cc::array<uint64_t, 3> fence_vals = {mFlushSignalVal, mFlushSignalVal, mFlushSignalVal};

    ID3D12Device5* const device = mDevice.getDevice5();
    PHI_D3D12_VERIFY(device->SetEventOnMultipleFenceCompletion(fences.data(), fence_vals.data(), 3, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, mFlushEvent));
    ::WaitForSingleObject(mFlushEvent, INFINITE);
}

phi::handle::resource phi::d3d12::BackendD3D12::acquireBackbuffer()
{
    auto const backbuffer_i = mSwapchain.waitForBackbuffer();
    auto const& backbuffer = mSwapchain.getBackbuffer(backbuffer_i);
    return mPoolResources.injectBackbufferResource(
        backbuffer.resource, backbuffer.state == D3D12_RESOURCE_STATE_RENDER_TARGET ? resource_state::render_target : resource_state::present);
}

void phi::d3d12::BackendD3D12::onResize(tg::isize2 size)
{
    flushGPU();
    onInternalResize();
    mSwapchain.onResize(size);
}

phi::format phi::d3d12::BackendD3D12::getBackbufferFormat() const { return util::to_pr_format(mSwapchain.getBackbufferFormat()); }

phi::handle::command_list phi::d3d12::BackendD3D12::recordCommandList(std::byte* buffer, size_t size, queue_type queue)
{
    auto& thread_comp = mThreadComponents[mThreadAssociation.get_current_index()];
    ID3D12GraphicsCommandList5* raw_list5;
    auto const res = mPoolCmdLists.create(raw_list5, thread_comp.cmd_list_allocator, queue);
    thread_comp.translator.translateCommandList(raw_list5, queue, mPoolCmdLists.getStateCache(res), buffer, size);
    return res;
}

void phi::d3d12::BackendD3D12::submit(cc::span<const phi::handle::command_list> cls, queue_type queue)
{
    constexpr auto c_batch_size = 16;
    ID3D12CommandQueue& target_queue = getQueueByType(queue);

    cc::capped_vector<ID3D12CommandList*, c_batch_size * 2> submit_batch;
    cc::capped_vector<handle::command_list, c_batch_size> barrier_lists;
    unsigned last_cl_index = 0;
    unsigned num_cls_in_batch = 0;

    auto& thread_comp = mThreadComponents[mThreadAssociation.get_current_index()];

    auto const submit_flush = [&]() {
        target_queue.ExecuteCommandLists(UINT(submit_batch.size()), submit_batch.data());
        mPoolCmdLists.freeOnSubmit(barrier_lists, target_queue);
        mPoolCmdLists.freeOnSubmit(cls.subspan(last_cl_index, num_cls_in_batch), target_queue);

        submit_batch.clear();
        barrier_lists.clear();
        last_cl_index += num_cls_in_batch;
        num_cls_in_batch = 0;
    };

    for (auto const cl : cls)
    {
        if (cl == handle::null_command_list)
            continue;

        auto const* const state_cache = mPoolCmdLists.getStateCache(cl);
        cc::capped_vector<D3D12_RESOURCE_BARRIER, 32> barriers;

        for (auto const& entry : state_cache->cache)
        {
            auto const master_before = mPoolResources.getResourceState(entry.ptr);

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
            ID3D12GraphicsCommandList5* t_cmd_list;
            barrier_lists.push_back(mPoolCmdLists.create(t_cmd_list, thread_comp.cmd_list_allocator, queue));
            t_cmd_list->ResourceBarrier(UINT(barriers.size()), barriers.size() > 0 ? barriers.data() : nullptr);
            t_cmd_list->Close();
            submit_batch.push_back(t_cmd_list);
        }

        submit_batch.push_back(mPoolCmdLists.getRawList(cl));
        ++num_cls_in_batch;

        if (num_cls_in_batch == c_batch_size)
            submit_flush();
    }

    if (num_cls_in_batch > 0)
        submit_flush();
}

phi::handle::pipeline_state phi::d3d12::BackendD3D12::createRaytracingPipelineState(arg::raytracing_shader_libraries libraries,
                                                                                    arg::raytracing_argument_associations arg_assocs,
                                                                                    arg::raytracing_hit_groups hit_groups,
                                                                                    unsigned max_recursion,
                                                                                    unsigned max_payload_size_bytes,
                                                                                    unsigned max_attribute_size_bytes)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    return mPoolPSOs.createRaytracingPipelineState(libraries, arg_assocs, hit_groups, max_recursion, max_payload_size_bytes, max_attribute_size_bytes);
}

phi::handle::accel_struct phi::d3d12::BackendD3D12::createTopLevelAccelStruct(unsigned num_instances)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    return mPoolAccelStructs.createTopLevelAS(num_instances);
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

void phi::d3d12::BackendD3D12::uploadTopLevelInstances(phi::handle::accel_struct as, cc::span<const phi::accel_struct_geometry_instance> instances)
{
    CC_ASSERT(isRaytracingEnabled() && "raytracing is not enabled");
    auto const& node = mPoolAccelStructs.getNode(as);
    std::memcpy(node.buffer_instances_map, instances.data(), sizeof(accel_struct_geometry_instance) * instances.size());
    // flushMappedMemory(node.buffer_instances); (no-op)
}

phi::handle::resource phi::d3d12::BackendD3D12::getAccelStructBuffer(phi::handle::accel_struct as) { return mPoolAccelStructs.getNode(as).buffer_as; }

phi::shader_table_sizes phi::d3d12::BackendD3D12::calculateShaderTableSize(phi::arg::shader_table_records ray_gen_records,
                                                                           phi::arg::shader_table_records miss_records,
                                                                           phi::arg::shader_table_records hit_group_records)
{
    return mShaderTableCtor.calculateShaderTableSizes(ray_gen_records, miss_records, hit_group_records);
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

void phi::d3d12::BackendD3D12::printInformation(phi::handle::resource res) const
{
    (void)res;
    log::info() << "printInformation unimplemented";
}

bool phi::d3d12::BackendD3D12::startForcedDiagnosticCapture() { return mDiagnostics.start_capture(); }

bool phi::d3d12::BackendD3D12::endForcedDiagnosticCapture() { return mDiagnostics.end_capture(); }

bool phi::d3d12::BackendD3D12::isRaytracingEnabled() const { return mDevice.hasRaytracing(); }
