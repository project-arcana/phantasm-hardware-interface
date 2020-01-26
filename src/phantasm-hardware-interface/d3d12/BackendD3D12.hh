#pragma once

#include <clean-core/fwd_array.hh>

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/detail/thread_association.hh>

#include "Adapter.hh"
#include "Device.hh"
#include "Queue.hh"
#include "Swapchain.hh"

#include "common/diagnostic_util.hh"
#include "pools/accel_struct_pool.hh"
#include "pools/cmd_list_pool.hh"
#include "pools/event_pool.hh"
#include "pools/pso_pool.hh"
#include "pools/resource_pool.hh"
#include "pools/shader_view_pool.hh"
#include "shader_table_construction.hh"

namespace phi::device
{
class Window;
};

namespace phi::d3d12
{
class BackendD3D12 final : public Backend
{
public:
    void initialize(backend_config const& config, window_handle const& window_handle) override;
    void destroy() override;
    ~BackendD3D12() override;

public:
    // Virtual interface

    void flushGPU() override;

    //
    // Swapchain interface
    //

    [[nodiscard]] handle::resource acquireBackbuffer() override;
    void present() override { mSwapchain.present(); }
    void onResize(tg::isize2 size) override;

    tg::isize2 getBackbufferSize() const override { return mSwapchain.getBackbufferSize(); }
    format getBackbufferFormat() const override;
    unsigned getNumBackbuffers() const override { return mSwapchain.getNumBackbuffers(); }

    //
    // Resource interface
    //

    [[nodiscard]] handle::resource createTexture(phi::format format, tg::isize2 size, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav) override
    {
        return mPoolResources.createTexture(format, static_cast<unsigned>(size.width), static_cast<unsigned>(size.height), mips, dim, depth_or_array_size, allow_uav);
    }

    [[nodiscard]] handle::resource createRenderTarget(phi::format format, tg::isize2 size, unsigned samples) override
    {
        return mPoolResources.createRenderTarget(format, static_cast<unsigned>(size.width), static_cast<unsigned>(size.height), samples);
    }

    [[nodiscard]] handle::resource createBuffer(unsigned size_bytes, unsigned stride_bytes, bool allow_uav) override
    {
        return mPoolResources.createBuffer(size_bytes, stride_bytes, allow_uav);
    }

    [[nodiscard]] handle::resource createMappedBuffer(unsigned size_bytes, unsigned stride_bytes = 0) override
    {
        return mPoolResources.createMappedBuffer(size_bytes, stride_bytes);
    }

    [[nodiscard]] std::byte* getMappedMemory(handle::resource res) override { return mPoolResources.getMappedMemory(res); }

    void flushMappedMemory(handle::resource /*res*/) override
    {
        // all d3d12 mapped buffers are host coherent, we don't have to do anything here
    }

    void free(handle::resource res) override { mPoolResources.free(res); }

    void freeRange(cc::span<handle::resource const> resources) override { mPoolResources.free(resources); }


    //
    // Shader view interface
    //

    [[nodiscard]] handle::shader_view createShaderView(cc::span<resource_view const> srvs,
                                                       cc::span<resource_view const> uavs,
                                                       cc::span<sampler_config const> samplers,
                                                       bool /*usage_compute*/) override
    {
        return mPoolShaderViews.create(srvs, uavs, samplers);
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
        return mPoolPSOs.createPipelineState(vertex_format, framebuffer_conf, shader_arg_shapes, has_root_constants, shaders, primitive_config);
    }

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::shader_arg_shapes shader_arg_shapes, arg::shader_binary shader, bool has_root_constants) override
    {
        return mPoolPSOs.createComputePipelineState(shader_arg_shapes, shader, has_root_constants);
    }

    void free(handle::pipeline_state ps) override { mPoolPSOs.free(ps); }

    //
    // Command list interface
    //

    [[nodiscard]] handle::command_list recordCommandList(std::byte* buffer, size_t size, handle::event event_to_set = handle::null_event) override;
    void discard(cc::span<handle::command_list const> cls) override { mPoolCmdLists.freeOnDiscard(cls); }
    void submit(cc::span<handle::command_list const> cls) override;

    //
    // Event interface
    //

    /// create an event, starts out unset
    [[nodiscard]] handle::event createEvent() override { return mPoolEvents.createEvent(); }

    /// if the event is set, unsets it and returns true, otherwise returns false
    bool clearEvent(handle::event event) override;

    void free(cc::span<handle::event const> events) override { mPoolEvents.free(events); }

    //
    // Raytracing interface
    //

    [[nodiscard]] handle::pipeline_state createRaytracingPipelineState(arg::raytracing_shader_libraries libraries,
                                                                       arg::raytracing_argument_associations arg_assocs,
                                                                       arg::raytracing_hit_groups hit_groups,
                                                                       unsigned max_recursion,
                                                                       unsigned max_payload_size_bytes,
                                                                       unsigned max_attribute_size_bytes) override;

    [[nodiscard]] handle::accel_struct createTopLevelAccelStruct(unsigned num_instances) override;

    [[nodiscard]] handle::accel_struct createBottomLevelAccelStruct(cc::span<arg::blas_element const> elements,
                                                                    accel_struct_build_flags_t flags,
                                                                    uint64_t* out_native_handle = nullptr) override;

    void uploadTopLevelInstances(handle::accel_struct as, cc::span<accel_struct_geometry_instance const> instances) override;

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

    void printInformation(handle::resource res) const override;
    bool startForcedDiagnosticCapture() override;
    bool endForcedDiagnosticCapture() override;

    //
    // GPU info interface
    //

    bool isRaytracingEnabled() const override;

public:
    // backend-internal

    [[nodiscard]] ID3D12Device& getDevice() { return mDevice.getDevice(); }
    [[nodiscard]] ID3D12CommandQueue& getDirectQueue() { return mGraphicsQueue.getQueue(); }


private:
    // Core components
    Adapter mAdapter;
    Device mDevice;
    Queue mGraphicsQueue;
    Swapchain mSwapchain;

    // Pools
    ResourcePool mPoolResources;
    CommandListPool mPoolCmdLists;
    PipelineStateObjectPool mPoolPSOs;
    ShaderViewPool mPoolShaderViews;
    EventPool mPoolEvents;
    AccelStructPool mPoolAccelStructs;

    // Logic
    struct per_thread_component;
    cc::fwd_array<per_thread_component> mThreadComponents;
    phi::detail::thread_association mThreadAssociation;
    ShaderTableConstructor mShaderTableCtor;

    // Misc
    util::diagnostic_state mDiagnostics;
};
}
