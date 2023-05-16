#pragma once

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/common/thread_association.hh>
#include <phantasm-hardware-interface/features/gpu_info.hh>
#include <phantasm-hardware-interface/types.hh>

#include "Device.hh"

#include "common/diagnostic_util.hh"
#include "pools/accel_struct_pool.hh"
#include "pools/cmd_list_pool.hh"
#include "pools/cmdlist_translator_pool.hh"
#include "pools/fence_pool.hh"
#include "pools/pipeline_pool.hh"
#include "pools/query_pool.hh"
#include "pools/resource_pool.hh"
#include "pools/shader_view_pool.hh"
#include "pools/swapchain_pool.hh"
#include "shader_table_construction.hh"

namespace phi::vk
{
class PHI_API BackendVulkan final : public Backend
{
public:
    init_status initialize(backend_config const& config_arg) override;
    void destroy() override;
    ~BackendVulkan() override;

public:
    // Virtual interface

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

    handle::accel_struct createTopLevelAccelStruct(uint32_t num_instances, accel_struct_build_flags_t flags, accel_struct_prebuild_info* out_prebuild_info = nullptr) override;

    handle::accel_struct createBottomLevelAccelStruct(cc::span<arg::blas_element const> elements,
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

    backend_type getBackendType() const override;

    gpu_info const& getGPUInfo() const override { return mGPUInfo; }

    VkInstance nativeGetInstance() { return mInstance; }

public:
    // backend-internal

    /// flush all pending work on the GPU
    void flushGPU() override;

private:
    void createDebugMessenger();

    struct per_thread_component;
    per_thread_component& getCurrentThreadComponent();

    cc::allocator* getCurrentScratchAlloc();

private:
    gpu_info mGPUInfo;
    VkInstance mInstance = nullptr;
    VkDebugUtilsMessengerEXT mDebugMessenger = nullptr;
    Device mDevice;

    // Pools
    ResourcePool mPoolResources;
    CommandListPool mPoolCmdLists;
    PipelinePool mPoolPipelines;
    ShaderViewPool mPoolShaderViews;
    FencePool mPoolFences;
    QueryPool mPoolQueries;
    AccelStructPool mPoolAccelStructs;
    SwapchainPool mPoolSwapchains;
    CmdlistTranslatorPool mPoolTranslators;

    // Logic
    per_thread_component* mThreadComponents;
    uint32_t mNumThreadComponents;
    void* mThreadComponentAlloc;
    phi::ThreadAssociation mThreadAssociation;
    ShaderTableConstructor mShaderTableCtor;

    // Misc
    util::diagnostic_state mDiagnostics;
};
} // namespace phi::vk
