#pragma once

#include <typed-geometry/types/size.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/fwd.hh>

namespace phi
{
enum class backend_type : uint8_t
{
    d3d12,
    vulkan
};

class Backend
{
public:
    virtual void initialize(backend_config const& config) = 0;
    virtual void destroy() = 0;

    virtual void flushGPU() = 0;

    //
    // Swapchain interface
    //

    /// create a swapchain on a given window
    [[nodiscard]] virtual handle::swapchain createSwapchain(window_handle const& window_handle,
                                                            tg::isize2 initial_size,
                                                            present_mode mode = present_mode::synced,
                                                            unsigned num_backbuffers = 3)
        = 0;

    /// destroy a swapchain
    virtual void free(handle::swapchain sc) = 0;

    /// acquire the next available backbuffer on the given swapchain
    /// blocks on CPU until a backbuffer is available
    /// if the returned handle is handle::null_resource, the current frame must be discarded
    /// can cause an internal resize on the swapchain
    [[nodiscard]] virtual handle::resource acquireBackbuffer(handle::swapchain sc) = 0;

    /// attempts to present on the swapchain
    /// can fail and cause an internal resize
    virtual void present(handle::swapchain sc) = 0;

    /// causes an internal resize on the swapchain
    virtual void onResize(handle::swapchain sc, tg::isize2 size) = 0;

    /// returns the current backbuffer size on the swapchain
    virtual tg::isize2 getBackbufferSize(handle::swapchain sc) const = 0;

    /// returns the backbuffer pixel format
    virtual format getBackbufferFormat(handle::swapchain sc) const = 0;

    /// returns the amount of backbuffers
    virtual unsigned getNumBackbuffers(handle::swapchain sc) const = 0;

    /// Clears pending internal resize events, returns true if the
    /// backbuffer has resized since the last call
    [[nodiscard]] virtual bool clearPendingResize(handle::swapchain sc) = 0;

    //
    // Resource interface
    //

    /// create a 1D, 2D or 3D texture, or a 1D/2D array
    /// if mips is 0, the maximum amount will be used
    /// if the texture will be used as a UAV, allow_uav must be true
    [[nodiscard]] virtual handle::resource createTexture(phi::format format,
                                                         tg::isize2 size,
                                                         unsigned mips,
                                                         texture_dimension dim = texture_dimension::t2d,
                                                         unsigned depth_or_array_size = 1,
                                                         bool allow_uav = false,
                                                         char const* debug_name = nullptr)
        = 0;

    /// create a [multisampled] 2D render- or depth-stencil target
    [[nodiscard]] virtual handle::resource createRenderTarget(phi::format format,
                                                              tg::isize2 size,
                                                              unsigned samples = 1,
                                                              unsigned array_size = 1,
                                                              rt_clear_value const* optimized_clear_val = nullptr,
                                                              char const* debug_name = nullptr)
        = 0;

    /// create a buffer with optional element stride, allocation on an upload/readback heap, or allowing UAV access
    [[nodiscard]] virtual handle::resource createBuffer(
        unsigned size_bytes, unsigned stride_bytes = 0, resource_heap heap = resource_heap::gpu, bool allow_uav = false, char const* debug_name = nullptr)
        = 0;

    /// create a buffer with optional element stride on resource_heap::upload (shorthand function)
    [[nodiscard]] virtual handle::resource createUploadBuffer(unsigned size_bytes, unsigned stride_bytes = 0) = 0;

    /// maps a buffer created on resource_heap::upload or ::readback to CPU-accessible memory and returns a pointer
    /// multiple (nested) maps are allowed, leaving a resource_heap::upload buffer persistently mapped is validW
    [[nodiscard]] virtual std::byte* mapBuffer(handle::resource res) = 0;

    /// unmaps a buffer, must have been previously mapped using mapBuffer
    /// it is not necessary to unmap a buffer before destruction
    /// on non-desktop it might be required to unmap upload buffers for the writes to become visible
    virtual void unmapBuffer(handle::resource res) = 0;

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
    [[nodiscard]] virtual handle::command_list recordCommandList(std::byte* buffer, size_t size, queue_type queue = queue_type::direct) = 0;

    /// destroy the given command list handles
    virtual void discard(cc::span<handle::command_list const> cls) = 0;

    /// submit and destroy the given command list handles
    virtual void submit(cc::span<handle::command_list const> cls, queue_type queue = queue_type::direct) = 0;

    //
    // Fence interface
    //

    /// create a fence, starts out with value 0
    [[nodiscard]] virtual handle::fence createFence() = 0;

    /// read the value of a fence
    [[nodiscard]] virtual uint64_t getFenceValue(handle::fence fence) = 0;

    /// signal a fence to a given value from CPU
    virtual void signalFenceCPU(handle::fence fence, uint64_t new_value) = 0;

    /// block on CPU until a fence reaches a given value
    virtual void waitFenceCPU(handle::fence fence, uint64_t wait_value) = 0;

    /// signal a fence to a given value from a specified GPU queue
    virtual void signalFenceGPU(handle::fence fence, uint64_t new_value, queue_type queue) = 0;

    /// block on a specified GPU queue until a fence reaches a given value
    virtual void waitFenceGPU(handle::fence fence, uint64_t wait_value, queue_type queue) = 0;

    virtual void free(cc::span<handle::fence const> fences) = 0;

    //
    // Query interface
    //

    [[nodiscard]] virtual handle::query_range createQueryRange(query_type type, unsigned size) = 0;

    virtual void free(handle::query_range query_range) = 0;

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

    /// returns the frequency of GPU timestamps in Hz (seconds = timestamp_delta / getGPUTimestampFrequency())
    virtual uint64_t getGPUTimestampFrequency() const = 0;

    virtual bool isRaytracingEnabled() const = 0;

    virtual backend_type getBackendType() const = 0;

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

    [[nodiscard]] handle::resource createBufferFromInfo(arg::create_buffer_info const& info, char const* debug_name = nullptr)
    {
        return createBuffer(info.size_bytes, info.stride_bytes, info.heap, info.allow_uav, debug_name);
    }

    [[nodiscard]] handle::resource createRenderTargetFromInfo(arg::create_render_target_info const& info, char const* debug_name = nullptr)
    {
        return createRenderTarget(info.format, {info.width, info.height}, info.num_samples, info.array_size, &info.clear_value, debug_name);
    }

    [[nodiscard]] handle::resource createTextureFromInfo(arg::create_texture_info const& info, char const* debug_name = nullptr)
    {
        return createTexture(info.fmt, {info.width, info.height}, info.num_mips, info.dim, info.depth_or_array_size, info.allow_uav, debug_name);
    }

    [[nodiscard]] handle::resource createResourceFromInfo(arg::create_resource_info const& info, char const* debug_name = nullptr)
    {
        switch (info.type)
        {
        case arg::create_resource_info::e_resource_render_target:
            return createRenderTargetFromInfo(info.info_render_target, debug_name);
        case arg::create_resource_info::e_resource_texture:
            return createTextureFromInfo(info.info_texture, debug_name);
        case arg::create_resource_info::e_resource_buffer:
            return createBufferFromInfo(info.info_buffer, debug_name);
        default:
            CC_ASSERT(false && "invalid type");
            return handle::null_resource;
        }
        CC_UNREACHABLE("invalid type");
    }

    Backend(Backend const&) = delete;
    Backend(Backend&&) = delete;
    Backend& operator=(Backend const&) = delete;
    Backend& operator=(Backend&&) = delete;
    virtual ~Backend() = default;

protected:
    Backend() = default;
};
}
