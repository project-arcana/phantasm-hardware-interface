#pragma once

#include <atomic>

#include <clean-core/fwd_array.hh>

#include <phantasm-hardware-interface/Backend.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/detail/thread_association.hh>

namespace phi::device
{
class Window;
};

namespace phi::debug
{
class BackendLog final : public Backend
{
public:
    void initialize(backend_config const& config, window_handle const& window_handle) override;
    void destroy() override;
    ~BackendLog() override;

public:
    // Virtual interface

    void flushGPU() override;

    //
    // Swapchain interface
    //

    [[nodiscard]] handle::resource acquireBackbuffer() override;
    void present() override;
    void onResize(tg::isize2 size) override;

    tg::isize2 getBackbufferSize() const override { return {100, 100}; }
    format getBackbufferFormat() const override { return format::rgba8un; }
    unsigned getNumBackbuffers() const override { return 4; }

    //
    // Resource interface
    //

    [[nodiscard]] handle::resource createTexture(phi::format format, tg::isize2 size, unsigned mips, texture_dimension dim, unsigned depth_or_array_size, bool allow_uav) override;

    [[nodiscard]] handle::resource createRenderTarget(phi::format format, tg::isize2 size, unsigned samples, rt_clear_value const* opt_clear) override;

    [[nodiscard]] handle::resource createBuffer(unsigned size_bytes, unsigned stride_bytes, bool allow_uav) override;

    [[nodiscard]] handle::resource createMappedBuffer(unsigned size_bytes, unsigned stride_bytes = 0) override;

    [[nodiscard]] std::byte* getMappedMemory(handle::resource res) override;

    void flushMappedMemory(handle::resource /*res*/) override;

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

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::shader_arg_shapes shader_arg_shapes, arg::shader_binary shader, bool has_root_constants) override;

    void free(handle::pipeline_state ps) override;

    //
    // Command list interface
    //

    [[nodiscard]] handle::command_list recordCommandList(std::byte* buffer, size_t size, handle::event event_to_set = handle::null_event) override;
    void discard(cc::span<handle::command_list const> cls) override;
    void submit(cc::span<handle::command_list const> cls) override;

    //
    // Event interface
    //

    /// create an event, starts out unset
    [[nodiscard]] handle::event createEvent() override;

    /// if the event is set, unsets it and returns true, otherwise returns false
    bool clearEvent(handle::event event) override;

    void free(cc::span<handle::event const> events) override;

    //
    // Raytracing interface
    //

    [[nodiscard]] handle::pipeline_state createRaytracingPipelineState(arg::raytracing_shader_libraries libraries,
                                                                       arg::raytracing_argument_associations arg_assocs,
                                                                       arg::raytracing_hit_groups hit_groups,
                                                                       unsigned max_recursion,
                                                                       unsigned max_payload_size_bytes,
                                                                       unsigned max_attribute_size_bytes) override
    {
        return phi::handle::null_pipeline_state;
    }


    [[nodiscard]] handle::accel_struct createTopLevelAccelStruct(unsigned num_instances) override { return phi::handle::null_accel_struct; }


    [[nodiscard]] handle::accel_struct createBottomLevelAccelStruct(cc::span<arg::blas_element const> elements,
                                                                    accel_struct_build_flags_t flags,
                                                                    uint64_t* out_native_handle = nullptr) override
    {
        return phi::handle::null_accel_struct;
    }

    void uploadTopLevelInstances(handle::accel_struct as, cc::span<accel_struct_geometry_instance const> instances) override {}

    [[nodiscard]] handle::resource getAccelStructBuffer(handle::accel_struct as) override { return phi::handle::null_resource; }

    [[nodiscard]] shader_table_sizes calculateShaderTableSize(arg::shader_table_records ray_gen_records,
                                                              arg::shader_table_records miss_records,
                                                              arg::shader_table_records hit_group_records) override
    {
        return {};
    }

    void writeShaderTable(std::byte* dest, handle::pipeline_state pso, unsigned stride, arg::shader_table_records records) override {}

    void free(handle::accel_struct as) override {}

    void freeRange(cc::span<handle::accel_struct const> as) override {}

    //
    // Debug interface
    //

    void printInformation(handle::resource res) const override {}
    bool startForcedDiagnosticCapture() override { return false; }
    bool endForcedDiagnosticCapture() override { return false; }

    //
    // GPU info interface
    //

    bool isRaytracingEnabled() const override { return false; }
    backend_type getBackendType() const override { return backend_type::d3d12; }

private:
    // Logic
    struct per_thread_component;
    cc::fwd_array<per_thread_component> mThreadComponents;

    // Dummy handle GUIDs
    struct
    {
        handle::resource backbuffer = {1 << 30};
        std::atomic<handle::index_t> resource_guid = {0};
        std::atomic<handle::index_t> pso_guid = {0};
        std::atomic<handle::index_t> sv_guid = {0};
        std::atomic<handle::index_t> cmdlist_guid = {0};
        std::atomic<handle::index_t> event_guid = {0};
    } mDummyGuids;

    std::atomic<unsigned> mMaxMappedSize = 0;
};
}
