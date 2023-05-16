#pragma once

#include <mutex>

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/common/thread_association.hh>

#include "Adapter.hh"
#include "Device.hh"
#include "Queue.hh"

#include "common/diagnostic_util.hh"
#include "pools/accel_struct_pool.hh"
#include "pools/cmd_list_pool.hh"
#include "pools/cmdlist_translator_pool.hh"
#include "pools/fence_pool.hh"
#include "pools/pso_pool.hh"
#include "pools/query_pool.hh"
#include "pools/resource_pool.hh"
#include "pools/shader_view_pool.hh"
#include "pools/swapchain_pool.hh"
#include "shader_table_construction.hh"

namespace phi::d3d12
{
class PHI_API BackendD3D12 final : public Backend
{
public:
    init_status initialize(backend_config const& config) override;
    init_status initializeParallel(backend_config const& config, uint32_t idx) override;
    init_status initializeQueues(backend_config const& config) override;
    void destroy() override;
    ~BackendD3D12() override;

public:
    // Virtual interface

    void flushGPU() override;

    //
    // Swapchain interface
    //

    [[nodiscard]] handle::swapchain createSwapchain(arg::swapchain_description const& desc, char const* debug_name = nullptr) override;

    void free(handle::swapchain sc) override;

    [[nodiscard]] handle::resource acquireBackbuffer(handle::swapchain sc) override;
    void present(handle::swapchain sc) override;
    void onResize(handle::swapchain sc, tg::isize2 size) override;

    tg::isize2 getBackbufferSize(handle::swapchain sc) const override
    {
        auto const& node = mPoolSwapchains.get(sc);
        return {node.backbuf_width, node.backbuf_height};
    }
    format getBackbufferFormat(handle::swapchain sc) const override;
    uint32_t getNumBackbuffers(handle::swapchain sc) const override { return uint32_t(mPoolSwapchains.get(sc).backbuffers.size()); }

    [[nodiscard]] bool clearPendingResize(handle::swapchain sc) override { return mPoolSwapchains.clearResizeFlag(sc); }

    //
    // Resource interface
    //

    [[nodiscard]] handle::resource createTexture(arg::texture_description const& desc, char const* debug_name = nullptr) override;

    [[nodiscard]] handle::resource createBuffer(arg::buffer_description const& desc, char const* debug_name = nullptr) override;

    [[nodiscard]] std::byte* mapBuffer(handle::resource res, int begin = 0, int end = -1) override;

    void unmapBuffer(handle::resource res, int begin = 0, int end = -1) override;

    void free(handle::resource res) override;

    void freeRange(cc::span<handle::resource const> resources) override;


    //
    // Shader view interface
    //

    [[nodiscard]] handle::shader_view createShaderView(cc::span<resource_view const> srvs,
                                                       cc::span<resource_view const> uavs,
                                                       cc::span<sampler_config const> samplers,
                                                       bool usage_compute) override;

    [[nodiscard]] handle::shader_view createEmptyShaderView(arg::shader_view_description const& desc, bool usage_compute) override;

    void writeShaderViewSRVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> srvs) override;
    void writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs) override;
    void writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers) override;

    void copyShaderViewSRVs(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors) override;
    void copyShaderViewUAVs(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors) override;
    void copyShaderViewSamplers(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors) override;

    void free(handle::shader_view sv) override;

    void freeRange(cc::span<handle::shader_view const> svs) override;

    //
    // Pipeline state interface
    //

    [[nodiscard]] handle::pipeline_state createPipelineState(arg::graphics_pipeline_state_description const& description, char const* debug_name = nullptr) override;

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::compute_pipeline_state_description const& description,
                                                                    char const* debug_name = nullptr) override;

    void free(handle::pipeline_state ps) override;

    //
    // Command list interface
    //

    [[nodiscard]] handle::command_list recordCommandList(std::byte const* buffer, size_t size, queue_type queue = queue_type::direct) override;
    void discard(cc::span<handle::command_list const> cls) override;
    void submit(cc::span<handle::command_list const> cls,
                queue_type queue = queue_type::direct,
                cc::span<fence_operation const> fence_waits_before = {},
                cc::span<fence_operation const> fence_signals_after = {}) override;

    //
    // Fence interface
    //

    /// create a fence, starts out with value 0
    [[nodiscard]] handle::fence createFence() override;

    [[nodiscard]] uint64_t getFenceValue(handle::fence fence) override;

    void signalFenceCPU(handle::fence fence, uint64_t new_value) override;

    void waitFenceCPU(handle::fence fence, uint64_t wait_value) override;

    // non-virtual d3d12 specific feature
    void signalFenceGPU(handle::fence fence, uint64_t new_value, queue_type queue);

    // non-virtual d3d12 specific feature
    void waitFenceGPU(handle::fence fence, uint64_t wait_value, queue_type queue);

    void free(cc::span<handle::fence const> fences) override;

    //
    // Query interface
    //

    [[nodiscard]] handle::query_range createQueryRange(query_type type, uint32_t size) override;

    void free(handle::query_range query_range) override;

    //
    // Raytracing interface
    //

    [[nodiscard]] handle::pipeline_state createRaytracingPipelineState(arg::raytracing_pipeline_state_description const& description,
                                                                       char const* debug_name = nullptr) override;

    [[nodiscard]] handle::accel_struct createTopLevelAccelStruct(uint32_t num_instances,
                                                                 accel_struct_build_flags_t flags,
                                                                 accel_struct_prebuild_info* out_prebuild_info = nullptr) override;

    [[nodiscard]] handle::accel_struct createBottomLevelAccelStruct(cc::span<arg::blas_element const> elements,
                                                                    accel_struct_build_flags_t flags,
                                                                    uint64_t* out_native_handle = nullptr,
                                                                    accel_struct_prebuild_info* out_prebuild_info = nullptr) override;

    [[nodiscard]] uint64_t getAccelStructNativeHandle(handle::accel_struct as) override;

    [[nodiscard]] shader_table_strides calculateShaderTableStrides(arg::shader_table_record const& ray_gen_record,
                                                                   arg::shader_table_records miss_records,
                                                                   arg::shader_table_records hit_group_records,
                                                                   arg::shader_table_records callable_records = {}) override;

    void writeShaderTable(std::byte* dest, handle::pipeline_state pso, uint32_t stride, arg::shader_table_records records) override;

    void free(handle::accel_struct as) override;

    void freeRange(cc::span<handle::accel_struct const> as) override;

    //
    // Live command list interface
    // Experimental API - subject to change
    //

    // start recording a commandlist directly
    // access to the live command list is not synchronized
    // NOTE: Only a single live command list can be active at a time per thread
    [[nodiscard]] handle::live_command_list openLiveCommandList(queue_type queue = queue_type::direct,
                                                                cmd::set_global_profile_scope const* opt_global_pscope = nullptr) override;

    // finish recording a commandlist - result can be submitted or discarded
    [[nodiscard]] handle::command_list closeLiveCommandList(handle::live_command_list list) override;

    void discardLiveCommandList(handle::live_command_list list) override;

    void cmdDraw(handle::live_command_list list, cmd::draw const& command) override;
    void cmdDrawIndirect(handle::live_command_list list, cmd::draw_indirect const& command) override;
    void cmdDispatch(handle::live_command_list list, cmd::dispatch const& command) override;
    void cmdDispatchIndirect(handle::live_command_list list, cmd::dispatch_indirect const& command) override;
    void cmdTransitionResources(handle::live_command_list list, cmd::transition_resources const& command) override;
    void cmdBarrierUAV(handle::live_command_list list, cmd::barrier_uav const& command) override;
    void cmdTransitionImageSlices(handle::live_command_list list, cmd::transition_image_slices const& command) override;
    void cmdCopyBuffer(handle::live_command_list list, cmd::copy_buffer const& command) override;
    void cmdCopyTexture(handle::live_command_list list, cmd::copy_texture const& command) override;
    void cmdCopyBufferToTexture(handle::live_command_list list, cmd::copy_buffer_to_texture const& command) override;
    void cmdCopyTextureToBuffer(handle::live_command_list list, cmd::copy_texture_to_buffer const& command) override;
    void cmdResolveTexture(handle::live_command_list list, cmd::resolve_texture const& command) override;
    void cmdBeginRenderPass(handle::live_command_list list, cmd::begin_render_pass const& command) override;
    void cmdEndRenderPass(handle::live_command_list list, cmd::end_render_pass const& command) override;
    void cmdWriteTimestamp(handle::live_command_list list, cmd::write_timestamp const& command) override;
    void cmdResolveQueries(handle::live_command_list list, cmd::resolve_queries const& command) override;
    void cmdBeginDebugLabel(handle::live_command_list list, cmd::begin_debug_label const& command) override;
    void cmdEndDebugLabel(handle::live_command_list list, cmd::end_debug_label const& command) override;
    void cmdUpdateBottomLevel(handle::live_command_list list, cmd::update_bottom_level const& command) override;
    void cmdUpdateTopLevel(handle::live_command_list list, cmd::update_top_level const& command) override;
    void cmdDispatchRays(handle::live_command_list list, cmd::dispatch_rays const& command) override;
    void cmdClearTextures(handle::live_command_list list, cmd::clear_textures const& command) override;
    void cmdBeginProfileScope(handle::live_command_list list, cmd::begin_profile_scope const& command) override;
    void cmdEndProfileScope(handle::live_command_list list, cmd::end_profile_scope const& command) override;

    //
    // Resource info interface
    //

    arg::resource_description const& getResourceDescription(handle::resource res) const override;
    arg::texture_description const& getResourceTextureDescription(handle::resource res) const override;
    arg::buffer_description const& getResourceBufferDescription(handle::resource res) const override;

    //
    // Debug interface
    //

    void setDebugName(handle::resource res, cc::string_view name) override;
    bool startForcedDiagnosticCapture() override;
    bool endForcedDiagnosticCapture() override;

    //
    // GPU info interface
    //

    clock_synchronization_info getClockSynchronizationInfo() override;

    uint64_t getGPUTimestampFrequency() const override;

    bool isRaytracingEnabled() const override;

    backend_type getBackendType() const override { return backend_type::d3d12; }

    gpu_info const& getGPUInfo() const override { return mAdapter.getGPUInfo(); }

public:
    // non virtual - d3d12 specific

    vram_state_info nativeGetVRAMStateInfo();

    ID3D12Device5* nativeGetDevice() const { return mDevice.getDevice(); }
    ID3D12CommandQueue* nativeGetDirectQueue() const { return mDirectQueue.command_queue; }
    ID3D12CommandQueue* nativeGetCopyQueue() const { return mCopyQueue.command_queue; }
    ID3D12CommandQueue* nativeGetComputeQueue() const { return mComputeQueue.command_queue; }

    ID3D12Resource* nativeGetResource(handle::resource res) const { return mPoolResources.getRawResource(res); }
    IDXGISwapChain3* nativeGetSwapchain(handle::swapchain sc) const { return mPoolSwapchains.get(sc).swapchain_com; }
    ID3D12GraphicsCommandList5* nativeGetCommandList(handle::command_list cl) const { return mPoolCmdLists.getRawList(cl); }

    // D3D11On12 objects, only valid if native_feature_d3d12_init_d3d11_on_12 is enabled
    ID3D11Device5* nativeGetD11Device() const { return mD11Device; }
    ID3D11DeviceContext4* nativeGetD11Context() const { return mD11Context; }
    ID3D11On12Device1* nativeGetD11On12() const { return mD11On12; }

    //
    // disable or clear D3D runtime- and driver-level PSO caches, requires running in developer mode
    // useful for profiling, especially startup and load times
    // requires running in developer mode and access to ID3D12Device9
    // affectD3DS: Disable or clear the D3D runtime cache and D3D runtime DXBC-to-DXIL cache (do not necessarily exist)
    // affectDriver: Hint the driver to disable or clear its internal cache (if it has one)
    // see https://microsoft.github.io/DirectX-Specs/d3d/ShaderCache.html for more info

    enum class pso_cache_control_action
    {
        INVALID = 0,
        disable = 0x1, // disable the caches - future PSO compilations won't read or write from them
        enable = 0x2,  // re-enable the caches (enabled by default)
        clear = 0x4,   // clear the caches - delete all existing contents
    };

    bool nativeControlPSOCaches(bool affectD3DS, bool affectDriver, pso_cache_control_action action);

private:
    ID3D12CommandQueue* getQueueByType(queue_type type) const
    {
        return (type == queue_type::direct ? mDirectQueue.command_queue : (type == queue_type::compute ? mComputeQueue.command_queue : mCopyQueue.command_queue));
    }

    struct per_thread_component;
    per_thread_component& getCurrentThreadComponent();

    cc::allocator* getCurrentScratchAlloc();

private:
    // Core components
    Adapter mAdapter;
    Device mDevice;

    Queue mDirectQueue;
    Queue mCopyQueue;
    Queue mComputeQueue;

    ::HANDLE mFlushEvent = nullptr;
    UINT64 mFlushSignalVal = 0;
    std::mutex mFlushMutex;

    // Pools
    SwapchainPool mPoolSwapchains;
    ResourcePool mPoolResources;
    CommandListPool mPoolCmdLists;
    PipelineStateObjectPool mPoolPSOs;
    ShaderViewPool mPoolShaderViews;
    FencePool mPoolFences;
    AccelStructPool mPoolAccelStructs;
    QueryPool mPoolQueries;
    CmdlistTranslatorPool mPoolTranslators;

    // Logic
    per_thread_component* mThreadComponents = nullptr;
    uint32_t mNumThreadComponents = 0;
    cc::allocator* mStaticAlloc = nullptr;
    phi::ThreadAssociation mThreadAssociation;
    ShaderTableConstructor mShaderTableCtor;
    cc::allocator* mDynamicAllocator = nullptr;

    // Misc
    util::diagnostic_state mDiagnostics;

    // D3D11On12
    ID3D11Device5* mD11Device = nullptr;
    ID3D11DeviceContext4* mD11Context = nullptr;
    ID3D11On12Device1* mD11On12 = nullptr;
};
} // namespace phi::d3d12
