#pragma once

#include <typed-geometry/types/size.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/fwd.hh>

namespace phi
{
class Backend
{
    // reference type
public:
    Backend(Backend const&) = delete;
    Backend(Backend&&) = delete;
    Backend& operator=(Backend const&) = delete;
    Backend& operator=(Backend&&) = delete;
    virtual ~Backend() = default;

    virtual void initialize(backend_config const& config, window_handle const& window_handle) = 0;
    virtual void destroy() = 0;

    virtual void flushGPU() = 0;

    //
    // Swapchain interface
    //

    /// acquires a resource handle for use as a render target
    /// if the returned handle is handle::null_resource, the current frame must be discarded
    /// can cause an internal resize
    [[nodiscard]] virtual handle::resource acquireBackbuffer() = 0;

    /// attempts to present,
    /// can fail and cause an internal resize
    virtual void present() = 0;

    /// causes an internal resize
    virtual void onResize(tg::isize2 size) = 0;

    /// returns the current backbuffer size
    virtual tg::isize2 getBackbufferSize() const = 0;

    /// returns the backbuffer pixel format
    virtual format getBackbufferFormat() const = 0;

    /// returns the amount of backbuffers
    virtual unsigned getNumBackbuffers() const = 0;

    /// Clears pending internal resize events, returns true if the
    /// backbuffer has resized since the last call
    [[nodiscard]] bool clearPendingResize()
    {
        if (mHasResized)
        {
            mHasResized = false;
            return true;
        }
        else
        {
            return false;
        }
    }

    //
    // Resource interface
    //

    /// create a 1D, 2D or 3D texture, or a 1D/2D array
    /// if mips is 0, the maximum amount will be used
    /// if the texture will be used as a UAV, allow_uav must be true
    [[nodiscard]] virtual handle::resource createTexture(
        phi::format format, tg::isize2 size, unsigned mips, texture_dimension dim = texture_dimension::t2d, unsigned depth_or_array_size = 1, bool allow_uav = false)
        = 0;

    /// create a [multisampled] 2D render- or depth-stencil target
    [[nodiscard]] virtual handle::resource createRenderTarget(phi::format format, tg::isize2 size, unsigned samples = 1) = 0;

    /// create a buffer, with an element stride if its an index or vertex buffer
    [[nodiscard]] virtual handle::resource createBuffer(unsigned size_bytes, unsigned stride_bytes = 0, bool allow_uav = false) = 0;

    /// create a mapped buffer for data uploads, with an element stride if its an index or vertex buffer
    [[nodiscard]] virtual handle::resource createMappedBuffer(unsigned size_bytes, unsigned stride_bytes = 0) = 0;

    /// returns the mapped memory pointer, only valid for handles obtained from createMappedBuffer
    virtual std::byte* getMappedMemory(handle::resource res) = 0;

    /// flushes writes to memory pointers received from getMappedMemory(res),
    /// making them GPU-visible in any successively submitted command lists (no-op on d3d12)
    virtual void flushMappedMemory(handle::resource res) = 0;

    /// destroy a resource
    virtual void free(handle::resource res) = 0;

    /// destroy multiple resources
    virtual void freeRange(cc::span<handle::resource const> resources) = 0;

    //
    // Shader view interface
    //

    [[nodiscard]] virtual handle::shader_view createShaderView(cc::span<resource_view const> srvs,
                                                               cc::span<resource_view const> uavs,
                                                               cc::span<sampler_config const> samplers,
                                                               bool usage_compute = false)
        = 0;

    virtual void free(handle::shader_view sv) = 0;

    virtual void freeRange(cc::span<handle::shader_view const> svs) = 0;

    //
    // Pipeline state interface
    //

    [[nodiscard]] virtual handle::pipeline_state createPipelineState(arg::vertex_format vertex_format,
                                                                     arg::framebuffer_config const& framebuffer_conf,
                                                                     arg::shader_arg_shapes shader_arg_shapes,
                                                                     bool has_root_constants,
                                                                     arg::graphics_shaders shaders,
                                                                     phi::pipeline_config const& primitive_config)
        = 0;

    [[nodiscard]] virtual handle::pipeline_state createComputePipelineState(arg::shader_arg_shapes shader_arg_shapes, arg::shader_binary shader, bool has_root_constants = false)
        = 0;

    virtual void free(handle::pipeline_state ps) = 0;

    //
    // Command list interface
    //

    /// create a command list handle from a software command buffer
    /// event_to_set: optional, will be set once the command list has finished executing (on the GPU, after submission)
    [[nodiscard]] virtual handle::command_list recordCommandList(std::byte* buffer, size_t size, handle::event event_to_set = handle::null_event) = 0;

    /// destroy the given command list handles
    virtual void discard(cc::span<handle::command_list const> cls) = 0;

    /// submit and destroy the given command list handles
    virtual void submit(cc::span<handle::command_list const> cls) = 0;

    //
    // Event interface
    //

    /// create an event, starts out unset
    [[nodiscard]] virtual handle::event createEvent() = 0;

    /// unsets the event, returns true if it was previously set, false otherwise
    virtual bool clearEvent(handle::event event) = 0;

    virtual void free(cc::span<handle::event const> events) = 0;

    //
    // Raytracing interface
    //

    [[nodiscard]] virtual handle::pipeline_state createRaytracingPipelineState(arg::raytracing_shader_libraries libraries,
                                                                               arg::raytracing_argument_associations arg_assocs,
                                                                               arg::raytracing_hit_groups hit_groups,
                                                                               unsigned max_recursion,
                                                                               unsigned max_payload_size_bytes,
                                                                               unsigned max_attribute_size_bytes)
        = 0;

    [[nodiscard]] virtual handle::accel_struct createTopLevelAccelStruct(unsigned num_instances) = 0;

    [[nodiscard]] virtual handle::accel_struct createBottomLevelAccelStruct(cc::span<arg::blas_element const> elements,
                                                                            accel_struct_build_flags_t flags,
                                                                            uint64_t* out_native_handle = nullptr)
        = 0;

    virtual void uploadTopLevelInstances(handle::accel_struct as, cc::span<accel_struct_geometry_instance const> instances) = 0;

    [[nodiscard]] virtual handle::resource getAccelStructBuffer(handle::accel_struct as) = 0;

    [[nodiscard]] virtual shader_table_sizes calculateShaderTableSize(arg::shader_table_records ray_gen_records,
                                                                      arg::shader_table_records miss_records,
                                                                      arg::shader_table_records hit_group_records)
        = 0;

    virtual void writeShaderTable(std::byte* dest, handle::pipeline_state pso, unsigned stride, arg::shader_table_records records) = 0;

    virtual void free(handle::accel_struct as) = 0;

    virtual void freeRange(cc::span<handle::accel_struct const> as) = 0;

    //
    // Debug interface
    //

    /// prints diagnostic information about the given resource
    virtual void printInformation(handle::resource res) const = 0;

    /// attempts to detect graphics diagnostic tools (PIX, NSight, Renderdoc)
    /// and forces a capture start, returns true on success
    virtual bool startForcedDiagnosticCapture() = 0;

    /// ends a previously started forced diagnostic capture,
    /// returns true on success
    virtual bool endForcedDiagnosticCapture() = 0;

    //
    // GPU info interface
    //

    virtual bool isRaytracingEnabled() const = 0;

    //
    // Non-virtual utility
    //

    /// free multiple handles of different types
    /// convenience, for more efficiency use freeRange
    template <class... Args>
    void freeVariadic(Args... handles)
    {
        (free(handles), ...);
    }

protected:
    Backend() = default;

    void onInternalResize() { mHasResized = true; }

private:
    bool mHasResized = true;
};
}
