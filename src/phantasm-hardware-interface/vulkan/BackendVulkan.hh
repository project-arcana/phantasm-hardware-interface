#pragma once

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/common/thread_association.hh>
#include <phantasm-hardware-interface/types.hh>

#include "Device.hh"

#include "common/diagnostic_util.hh"
#include "pools/accel_struct_pool.hh"
#include "pools/cmd_list_pool.hh"
#include "pools/fence_pool.hh"
#include "pools/pipeline_pool.hh"
#include "pools/query_pool.hh"
#include "pools/resource_pool.hh"
#include "pools/shader_view_pool.hh"
#include "pools/swapchain_pool.hh"

namespace phi::vk
{
class BackendVulkan final : public Backend
{
public:
    void initialize(backend_config const& config_arg) override;
    void destroy() override;
    ~BackendVulkan() override;

public:
    // Virtual interface

    //
    // Swapchain interface
    //

    [[nodiscard]] handle::swapchain createSwapchain(window_handle const& window_handle,
                                                    tg::isize2 initial_size,
                                                    present_mode mode = present_mode::synced,
                                                    unsigned num_backbuffers = 3) override;

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

    unsigned getNumBackbuffers(handle::swapchain sc) const override { return unsigned(mPoolSwapchains.get(sc).backbuffers.size()); }

    [[nodiscard]] bool clearPendingResize(handle::swapchain sc) override { return mPoolSwapchains.clearResizeFlag(sc); }


    //
    // Resource interface
    //

    [[nodiscard]] handle::resource createTexture(
        phi::format format, tg::isize2 size, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav, char const* debug_name = nullptr) override;

    [[nodiscard]] handle::resource createRenderTarget(
        phi::format format, tg::isize2 size, unsigned samples, unsigned array_size, rt_clear_value const* = nullptr, char const* debug_name = nullptr) override;

    [[nodiscard]] handle::resource createBuffer(unsigned int size_bytes, unsigned int stride_bytes, resource_heap heap, bool allow_uav, char const* debug_name = nullptr) override;

    [[nodiscard]] handle::resource createUploadBuffer(unsigned size_bytes, unsigned stride_bytes = 0, char const* debug_name = nullptr) override;

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
                                                             phi::pipeline_config const& primitive_config) override;

    [[nodiscard]] handle::pipeline_state createPipelineState(arg::graphics_pipeline_state_desc const& description) override;

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::shader_arg_shapes shader_arg_shapes, arg::shader_binary shader, bool has_root_constants) override;

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::compute_pipeline_state_desc const& description) override;

    void free(handle::pipeline_state ps) override;

    //
    // Command list interface
    //

    [[nodiscard]] handle::command_list recordCommandList(std::byte* buffer, size_t size, queue_type queue = queue_type::direct) override;
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

    [[nodiscard]] handle::query_range createQueryRange(query_type type, unsigned int size) override;

    void free(handle::query_range query_range) override;

    //
    // Raytracing interface
    //

    [[nodiscard]] handle::pipeline_state createRaytracingPipelineState(arg::raytracing_pipeline_state_desc const& description) override;

    handle::accel_struct createTopLevelAccelStruct(unsigned num_instances, accel_struct_build_flags_t flags) override;

    handle::accel_struct createBottomLevelAccelStruct(cc::span<arg::blas_element const> elements,
                                                      accel_struct_build_flags_t flags,
                                                      uint64_t* out_native_handle = nullptr) override;

    [[nodiscard]] handle::resource getAccelStructBuffer(handle::accel_struct as) override;

    [[nodiscard]] uint64_t getAccelStructNativeHandle(handle::accel_struct as) override;

    [[nodiscard]] shader_table_strides calculateShaderTableStrides(arg::shader_table_record const& ray_gen_record,
                                                                   arg::shader_table_records miss_records,
                                                                   arg::shader_table_records hit_group_records,
                                                                   arg::shader_table_records callable_records = {}) override;

    void writeShaderTable(std::byte* dest, handle::pipeline_state pso, unsigned stride, arg::shader_table_records records) override;

    void free(handle::accel_struct as) override;

    void freeRange(cc::span<handle::accel_struct const> as) override;

    //
    // Debug interface
    //

    void setDebugName(handle::resource res, cc::string_view name) override;

    void printInformation(handle::resource res) const override;

    bool startForcedDiagnosticCapture() override;

    bool endForcedDiagnosticCapture() override;

    //
    // GPU info interface
    //

    uint64_t getGPUTimestampFrequency() const override;

    bool isRaytracingEnabled() const override;

    backend_type getBackendType() const override;

public:
    // backend-internal

    /// flush all pending work on the GPU
    void flushGPU() override;

private:
    void createDebugMessenger();

    struct per_thread_component;
    per_thread_component& getCurrentThreadComponent();

private:
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

    // Logic
    per_thread_component* mThreadComponents;
    unsigned mNumThreadComponents;
    void* mThreadComponentAlloc;
    phi::thread_association mThreadAssociation;

    // Misc
    util::diagnostic_state mDiagnostics;
};
}
