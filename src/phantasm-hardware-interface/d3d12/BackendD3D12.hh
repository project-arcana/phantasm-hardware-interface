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
    void initialize(backend_config const& config) override;
    void initializeParallel(backend_config const& config, uint32_t idx) override;
    void destroy() override;
    ~BackendD3D12() override;

public:
    // Virtual interface

    void flushGPU() override;

    //
    // Swapchain interface
    //

    [[nodiscard]] handle::swapchain createSwapchain(window_handle const& window_handle,
                                                    tg::isize2 initial_size,
                                                    present_mode mode = present_mode::synced,
                                                    uint32_t num_backbuffers = 3) override;

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

    void free(handle::shader_view sv) override;

    void freeRange(cc::span<handle::shader_view const> svs) override;

    //
    // Pipeline state interface
    //

    [[nodiscard]] handle::pipeline_state createPipelineState(arg::vertex_format vertex_format,
                                                             arg::framebuffer_config const& framebuffer_conf,
                                                             arg::shader_arg_shapes shader_arg_shapes,
                                                             bool has_root_constants,
                                                             arg::graphics_shaders shaders,
                                                             phi::pipeline_config const& primitive_config,
                                                             char const* debug_name = nullptr) override;

    [[nodiscard]] handle::pipeline_state createPipelineState(arg::graphics_pipeline_state_description const& description, char const* debug_name = nullptr) override;

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::shader_arg_shapes shader_arg_shapes,
                                                                    arg::shader_binary shader,
                                                                    bool has_root_constants,
                                                                    char const* debug_name = nullptr) override;

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

    uint64_t getGPUTimestampFrequency() const override;

    bool isRaytracingEnabled() const override;

    backend_type getBackendType() const override { return backend_type::d3d12; }

    gpu_info const& getGPUInfo() const override { return mAdapter.getGPUInfo(); }

public:
    // non virtual - d3d12 specific

    vram_state_info nativeGetVRAMStateInfo();

    ID3D12Device5* nativeGetDevice() { return mDevice.getDevice(); }
    ID3D12CommandQueue* nativeGetDirectQueue() { return mDirectQueue.command_queue; }
    ID3D12CommandQueue* nativeGetCopyQueue() { return mCopyQueue.command_queue; }
    ID3D12CommandQueue* nativeGetComputeQueue() { return mComputeQueue.command_queue; }

    ID3D12Resource* nativeGetResource(handle::resource res) { return mPoolResources.getRawResource(res); }
    IDXGISwapChain3* nativeGetSwapchain(handle::swapchain sc) { return mPoolSwapchains.get(sc).swapchain_com; }
    ID3D12GraphicsCommandList5* nativeGetCommandList(handle::command_list cl) { return mPoolCmdLists.getRawList(cl); }

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

    // Logic
    per_thread_component* mThreadComponents = nullptr;
    uint32_t mNumThreadComponents = 0;
    cc::allocator* mStaticAlloc = nullptr;
    phi::thread_association mThreadAssociation;
    ShaderTableConstructor mShaderTableCtor;
    cc::allocator* mDynamicAllocator = nullptr;

    // Misc
    util::diagnostic_state mDiagnostics;
};
} // namespace phi::d3d12
