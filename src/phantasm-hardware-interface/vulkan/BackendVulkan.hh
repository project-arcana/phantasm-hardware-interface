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

namespace phi::device
{
class Window;
};

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

    void free(handle::swapchain sc) override { mPoolSwapchains.free(sc); }

    [[nodiscard]] handle::resource acquireBackbuffer(handle::swapchain sc) override;
    void present(handle::swapchain sc) override { mPoolSwapchains.present(sc); }
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
        phi::format format, tg::isize2 size, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav, char const* debug_name = nullptr) override
    {
        return mPoolResources.createTexture(format, unsigned(size.width), unsigned(size.height), mips, dim, depth_or_array_size, allow_uav, debug_name);
    }

    [[nodiscard]] handle::resource createRenderTarget(
        phi::format format, tg::isize2 size, unsigned samples, unsigned array_size, rt_clear_value const* = nullptr, char const* debug_name = nullptr) override
    {
        return mPoolResources.createRenderTarget(format, unsigned(size.width), unsigned(size.height), samples, array_size, debug_name);
    }

    [[nodiscard]] handle::resource createBuffer(unsigned int size_bytes, unsigned int stride_bytes, resource_heap heap, bool allow_uav, char const* debug_name = nullptr) override
    {
        return mPoolResources.createBuffer(size_bytes, stride_bytes, heap, allow_uav, debug_name);
    }

    [[nodiscard]] handle::resource createUploadBuffer(unsigned size_bytes, unsigned stride_bytes = 0) override
    {
        return createBuffer(size_bytes, stride_bytes, resource_heap::upload, false);
    }

    [[nodiscard]] std::byte* mapBuffer(handle::resource res, int begin = 0, int end = -1) override
    {
        return mPoolResources.mapBuffer(res, begin, end);
    }

    void unmapBuffer(handle::resource res, int begin = 0, int end = -1) override { return mPoolResources.unmapBuffer(res, begin, end); }

    void free(handle::resource res) override { mPoolResources.free(res); }
    void freeRange(cc::span<handle::resource const> resources) override { mPoolResources.free(resources); }


    //
    // Shader view interface
    //

    [[nodiscard]] handle::shader_view createShaderView(cc::span<resource_view const> srvs,
                                                       cc::span<resource_view const> uavs,
                                                       cc::span<sampler_config const> samplers,
                                                       bool usage_compute) override
    {
        return mPoolShaderViews.create(srvs, uavs, samplers, usage_compute);
    }

    void free(handle::shader_view sv) override { mPoolShaderViews.free(sv); }

    void freeRange(cc::span<handle::shader_view const> svs) override { mPoolShaderViews.free(svs); }

    //
    // Pipeline state interface
    //

    [[nodiscard]] handle::pipeline_state createPipelineState(arg::vertex_format vertex_format,
                                                             arg::framebuffer_config const& framebuffer_conf,
                                                             arg::shader_arg_shapes shader_arg_shapes,
                                                             bool has_root_constants,
                                                             arg::graphics_shaders shaders,
                                                             phi::pipeline_config const& primitive_config) override
    {
        return mPoolPipelines.createPipelineState(vertex_format, framebuffer_conf, shader_arg_shapes, has_root_constants, shaders, primitive_config,
                                                  cc::system_allocator);
    }

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::shader_arg_shapes shader_arg_shapes, arg::shader_binary shader, bool has_root_constants) override
    {
        return mPoolPipelines.createComputePipelineState(shader_arg_shapes, shader, has_root_constants, cc::system_allocator);
    }

    void free(handle::pipeline_state ps) override { mPoolPipelines.free(ps); }

    //
    // Command list interface
    //

    [[nodiscard]] handle::command_list recordCommandList(std::byte* buffer, size_t size, queue_type queue = queue_type::direct) override;
    void discard(cc::span<handle::command_list const> cls) override { mPoolCmdLists.freeAndDiscard(cls); }

    void submit(cc::span<handle::command_list const> cls,
                queue_type queue = queue_type::direct,
                cc::span<fence_operation const> fence_waits_before = {},
                cc::span<fence_operation const> fence_signals_after = {}) override;

    //
    // Fence interface
    //

    /// create a fence, starts out with value 0
    [[nodiscard]] handle::fence createFence() override { return mPoolFences.createFence(); }

    [[nodiscard]] uint64_t getFenceValue(handle::fence fence) override { return mPoolFences.getValue(fence); }

    void signalFenceCPU(handle::fence fence, uint64_t new_value) override { mPoolFences.signalCPU(fence, new_value); }

    void waitFenceCPU(handle::fence fence, uint64_t wait_value) override { mPoolFences.waitCPU(fence, wait_value); }

    void free(cc::span<handle::fence const> fences) override { mPoolFences.free(fences); }


    //
    // Query interface
    //

    [[nodiscard]] handle::query_range createQueryRange(query_type type, unsigned int size) override { return mPoolQueries.create(type, size); }

    void free(handle::query_range query_range) override { mPoolQueries.free(query_range); }

    //
    // Raytracing interface
    //

    [[nodiscard]] handle::pipeline_state createRaytracingPipelineState(arg::raytracing_shader_libraries libraries,
                                                                       arg::raytracing_argument_associations arg_assocs,
                                                                       arg::raytracing_hit_groups hit_groups,
                                                                       unsigned max_recursion,
                                                                       unsigned max_payload_size_bytes,
                                                                       unsigned max_attribute_size_bytes) override;

    handle::accel_struct createTopLevelAccelStruct(unsigned num_instances) override;

    handle::accel_struct createBottomLevelAccelStruct(cc::span<arg::blas_element const> elements,
                                                      accel_struct_build_flags_t flags,
                                                      uint64_t* out_native_handle = nullptr) override;

    void uploadTopLevelInstances(handle::accel_struct as, cc::span<accel_struct_instance const> instances) override;

    [[nodiscard]] handle::resource getAccelStructBuffer(handle::accel_struct as) override;

    [[nodiscard]] shader_table_sizes calculateShaderTableSize(arg::shader_table_records ray_gen_records,
                                                              arg::shader_table_records miss_records,
                                                              arg::shader_table_records hit_group_records) override;

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

    uint64_t getGPUTimestampFrequency() const override
    {
        float const nanoseconds_per_timestamp = mDevice.getDeviceProperties().limits.timestampPeriod;
        uint64_t const timestamps_per_microsecond = uint64_t(1000.f / nanoseconds_per_timestamp);
        // us -> ms -> s (Hz)
        return timestamps_per_microsecond * 1000 * 1000;
    }

    bool isRaytracingEnabled() const override { return mDevice.hasRaytracing(); }

    backend_type getBackendType() const override { return backend_type::vulkan; }

public:
    // backend-internal

    /// flush all pending work on the GPU
    void flushGPU() override;

private:
    void createDebugMessenger();

    VkQueue getQueueByType(queue_type type) const
    {
        return (type == queue_type::direct ? mDevice.getQueueDirect() : (type == queue_type::compute ? mDevice.getQueueCompute() : mDevice.getQueueCopy()));
    }

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
