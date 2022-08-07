#pragma once

#include <clean-core/span.hh>

#include <typed-geometry/types/size.hh>

#include <phantasm-hardware-interface/common/api.hh>
#include <phantasm-hardware-interface/fwd.hh>
#include <phantasm-hardware-interface/types.hh>

namespace phi
{
enum class backend_type : uint32_t
{
    d3d12,
    vulkan
};

enum class init_status
{
    success = 0,
    // No GPU surpassing the minspec was found, or the explicitly specified GPU was not found or is unsupported
    err_no_gpu_eligible,
    // The operating system is older than the minimum supported version, or was the cause for a fatal error
    err_operating_system,
    // The GPU drivers are missing, out of date, or were the cause for a fatal error
    err_drivers,
    // The native API runtime is missing, out of date, or was the cause for a fatal error
    err_runtime,
    // An unspecified fatal error occured
    err_unexpected,
};

class PHI_API Backend
{
public:
    virtual init_status initialize(backend_config const& config) = 0;
    virtual void destroy() = 0;

    /// parallel init: If enabled, call this N times after the main call to initialize()
    /// call with indices 0 to num_threads - 1 and with the same config as in the original initialize()
    /// intended to be called in parallel
    virtual init_status initializeParallel(backend_config const& config, uint32_t idx);

    /// delayed queue init: If enabled, call this after the main call to initialize()
    /// creating queues takes up about 30% of init time and can be delayed in order to start earlier with PSO compiles
    /// must only use initializeParallel and PSO creation before this is called
    virtual init_status initializeQueues(backend_config const& config);

    virtual void flushGPU() = 0;

    //
    // Swapchain interface
    //

    /// create a swapchain on a given window
    [[nodiscard]] virtual handle::swapchain createSwapchain(window_handle const& window_handle,
                                                            tg::isize2 initial_size,
                                                            present_mode mode = present_mode::synced,
                                                            uint32_t num_backbuffers = 3)
        = 0;

    /// destroy a swapchain
    virtual void free(handle::swapchain sc) = 0;

    /// acquire the next available backbuffer on the given swapchain
    /// if the returned handle is handle::null_resource, the current frame must be discarded
    /// can cause an internal resize on the swapchain
    [[nodiscard]] virtual handle::resource acquireBackbuffer(handle::swapchain sc) = 0;

    /// attempts to present on the swapchain (blocking)
    /// can fail and cause an internal resize
    virtual void present(handle::swapchain sc) = 0;

    /// causes an internal resize on the swapchain
    virtual void onResize(handle::swapchain sc, tg::isize2 size) = 0;

    /// returns the current backbuffer size on the swapchain
    virtual tg::isize2 getBackbufferSize(handle::swapchain sc) const = 0;

    /// returns the backbuffer pixel format
    virtual format getBackbufferFormat(handle::swapchain sc) const = 0;

    /// returns the amount of backbuffers
    virtual uint32_t getNumBackbuffers(handle::swapchain sc) const = 0;

    /// Clears pending internal resize events, returns true if the
    /// backbuffer has resized since the last call
    [[nodiscard]] virtual bool clearPendingResize(handle::swapchain sc) = 0;

    //
    // Resource interface
    //

    /// create a 1D, 2D or 3D texture, or a 1D/2D texture array
    /// for render- or depth targets, set the allow_render_target / allow_depth_stencil usage flags
    /// for UAV usage, set the allow_uav usage flag
    /// if mips is 0, the maximum amount will be used
    [[nodiscard]] virtual handle::resource createTexture(arg::texture_description const& desc, char const* debug_name = nullptr) = 0;

    /// create a buffer with optional element stride, allocation on an upload/readback heap, or allowing UAV access
    [[nodiscard]] virtual handle::resource createBuffer(arg::buffer_description const& info, char const* debug_name = nullptr) = 0;

    /// maps a buffer created on resource_heap::upload or ::readback to CPU-accessible memory and returns a pointer
    /// multiple (nested) maps are allowed, leaving a resource_heap::upload buffer persistently mapped is valid
    /// invalidate_begin and invalidate_end specify the range of data that will be read on CPU (in bytes), end == -1 being the entire width
    /// if the memory will only be written to, disable invalidation by setting both to 0
    /// NOTE: begin > 0 does not add an offset to the returned pointer
    [[nodiscard]] virtual std::byte* mapBuffer(handle::resource res, int invalidate_begin = 0, int invalidate_end = -1) = 0;

    /// unmaps a buffer, must have been previously mapped using mapBuffer
    /// it is not necessary to unmap a buffer before destruction
    /// on non-desktop it might be required to unmap upload buffers for the writes to become visible
    /// flush_begin and flush_end specify the range of CPU-side modified data in bytes, end == -1 being the entire width
    /// if the memory was only read from, disable flushing by setting both to 0
    virtual void unmapBuffer(handle::resource res, int flush_begin = 0, int flush_end = -1) = 0;

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

    /// create an empty shader view without specific resources written to it
    [[nodiscard]] virtual handle::shader_view createEmptyShaderView(arg::shader_view_description const& desc, bool usage_compute = false) = 0;

    /// write resources as contiguous SRVs to a shader view at a specified offset
    /// SRVs are indexed flat, meaning descriptor arrays are treated as sequential regular descriptors
    virtual void writeShaderViewSRVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> srvs) = 0;

    /// write resources as contiguous UAVs to a shader view at a specified offset
    /// UAVs are indexed flat, meaning descriptor arrays are treated as sequential regular descriptors
    virtual void writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs) = 0;

    virtual void writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers) = 0;

    virtual void free(handle::shader_view sv) = 0;

    virtual void freeRange(cc::span<handle::shader_view const> svs) = 0;

    //
    // Pipeline state interface
    //

    /// create a graphics pipeline state
    [[nodiscard]] virtual handle::pipeline_state createPipelineState(arg::graphics_pipeline_state_description const& description, char const* debug_name = nullptr)
        = 0;

    /// create a compute pipeline state
    [[nodiscard]] virtual handle::pipeline_state createComputePipelineState(arg::compute_pipeline_state_description const& description, char const* debug_name = nullptr)
        = 0;

    virtual void free(handle::pipeline_state ps) = 0;

    //
    // Command list interface
    //

    /// create a command list handle from a software command buffer
    [[nodiscard]] virtual handle::command_list recordCommandList(std::byte const* buffer, size_t size, queue_type queue = queue_type::direct) = 0;


    /// destroy the given command list handles
    virtual void discard(cc::span<handle::command_list const> cls) = 0;

    /// submit and destroy the given command list handles on a specified queue
    /// waiting on GPU for given fences before execution, and signalling fences on GPU after the commandlists have completed
    virtual void submit(cc::span<handle::command_list const> cls,
                        queue_type queue = queue_type::direct,
                        cc::span<fence_operation const> fence_waits_before = {},
                        cc::span<fence_operation const> fence_signals_after = {})
        = 0;

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

    virtual void free(cc::span<handle::fence const> fences) = 0;

    //
    // Query interface
    //

    [[nodiscard]] virtual handle::query_range createQueryRange(query_type type, uint32_t size) = 0;

    virtual void free(handle::query_range query_range) = 0;

    //
    // Raytracing interface
    //

    [[nodiscard]] virtual handle::pipeline_state createRaytracingPipelineState(arg::raytracing_pipeline_state_description const& description,
                                                                               char const* debug_name = nullptr)
        = 0;

    /// create a bottom level acceleration structure (BLAS) holding geometry elements
    /// out_native_handle receives the value to be written to accel_struct_instance::native_bottom_level_as_handle
    [[nodiscard]] virtual handle::accel_struct createBottomLevelAccelStruct(cc::span<arg::blas_element const> elements,
                                                                            accel_struct_build_flags_t flags,
                                                                            uint64_t* out_native_handle = nullptr,
                                                                            accel_struct_prebuild_info* out_prebuild_info = nullptr)
        = 0;

    /// create a top level acceleration structure (TLAS) holding BLAS instances
    [[nodiscard]] virtual handle::accel_struct createTopLevelAccelStruct(uint32_t num_instances,
                                                                         accel_struct_build_flags_t flags,
                                                                         accel_struct_prebuild_info* out_prebuild_info = nullptr)
        = 0;

    /// receive the native acceleration struct handle to be written to accel_struct_instance::native_bottom_level_as_handle
    [[nodiscard]] virtual uint64_t getAccelStructNativeHandle(handle::accel_struct as) = 0;

    /// calculate the buffer sizes and strides to accomodate the given shader table records
    [[nodiscard]] virtual shader_table_strides calculateShaderTableStrides(arg::shader_table_record const& ray_gen_record,
                                                                           arg::shader_table_records miss_records,
                                                                           arg::shader_table_records hit_group_records,
                                                                           arg::shader_table_records callable_records = {})
        = 0;

    /// write shader table records to memory - usually a mapped buffer
    virtual void writeShaderTable(std::byte* dest, handle::pipeline_state pso, uint32_t stride, arg::shader_table_records records) = 0;

    virtual void free(handle::accel_struct as) = 0;

    virtual void freeRange(cc::span<handle::accel_struct const> as) = 0;

    //
    // Live command list interface
    // Experimental API - subject to change
    //

    // start recording a commandlist directly
    // access to the live command list is not synchronized
    [[nodiscard]] virtual handle::live_command_list openLiveCommandList(queue_type queue = queue_type::direct,
                                                                        cmd::set_global_profile_scope const* opt_global_pscope = nullptr)
        = 0;

    // finish recording a commandlist - result can be submitted or discarded
    [[nodiscard]] virtual handle::command_list closeLiveCommandList(handle::live_command_list list) = 0;

    // abort recording a command list
    virtual void discardLiveCommandList(handle::live_command_list list) = 0;

    virtual void cmdDraw(handle::live_command_list list, cmd::draw const& command) = 0;
    virtual void cmdDrawIndirect(handle::live_command_list list, cmd::draw_indirect const& command) = 0;
    virtual void cmdDispatch(handle::live_command_list list, cmd::dispatch const& command) = 0;
    virtual void cmdDispatchIndirect(handle::live_command_list list, cmd::dispatch_indirect const& command) = 0;
    virtual void cmdTransitionResources(handle::live_command_list list, cmd::transition_resources const& command) = 0;
    virtual void cmdBarrierUAV(handle::live_command_list list, cmd::barrier_uav const& command) = 0;
    virtual void cmdTransitionImageSlices(handle::live_command_list list, cmd::transition_image_slices const& command) = 0;
    virtual void cmdCopyBuffer(handle::live_command_list list, cmd::copy_buffer const& command) = 0;
    virtual void cmdCopyTexture(handle::live_command_list list, cmd::copy_texture const& command) = 0;
    virtual void cmdCopyBufferToTexture(handle::live_command_list list, cmd::copy_buffer_to_texture const& command) = 0;
    virtual void cmdCopyTextureToBuffer(handle::live_command_list list, cmd::copy_texture_to_buffer const& command) = 0;
    virtual void cmdResolveTexture(handle::live_command_list list, cmd::resolve_texture const& command) = 0;
    virtual void cmdBeginRenderPass(handle::live_command_list list, cmd::begin_render_pass const& command) = 0;
    virtual void cmdEndRenderPass(handle::live_command_list list, cmd::end_render_pass const& command) = 0;
    virtual void cmdWriteTimestamp(handle::live_command_list list, cmd::write_timestamp const& command) = 0;
    virtual void cmdResolveQueries(handle::live_command_list list, cmd::resolve_queries const& command) = 0;
    virtual void cmdBeginDebugLabel(handle::live_command_list list, cmd::begin_debug_label const& command) = 0;
    virtual void cmdEndDebugLabel(handle::live_command_list list, cmd::end_debug_label const& command) = 0;
    virtual void cmdUpdateBottomLevel(handle::live_command_list list, cmd::update_bottom_level const& command) = 0;
    virtual void cmdUpdateTopLevel(handle::live_command_list list, cmd::update_top_level const& command) = 0;
    virtual void cmdDispatchRays(handle::live_command_list list, cmd::dispatch_rays const& command) = 0;
    virtual void cmdClearTextures(handle::live_command_list list, cmd::clear_textures const& command) = 0;
    virtual void cmdBeginProfileScope(handle::live_command_list list, cmd::begin_profile_scope const& command) = 0;
    virtual void cmdEndProfileScope(handle::live_command_list list, cmd::end_profile_scope const& command) = 0;

    //
    // Resource info interface
    //

    virtual arg::resource_description const& getResourceDescription(handle::resource res) const = 0;

    virtual arg::texture_description const& getResourceTextureDescription(handle::resource res) const = 0;

    virtual arg::buffer_description const& getResourceBufferDescription(handle::resource res) const = 0;

    //
    // Debug interface
    //

    /// resets the debug name of a resource
    /// this is the name visible to diagnostic tools and referred to by validation warnings
    virtual void setDebugName(handle::resource res, cc::string_view name) = 0;

    /// attempts to detect graphics diagnostic tools (PIX, NSight, Renderdoc)
    /// and forces a capture start, returns true on success
    virtual bool startForcedDiagnosticCapture() = 0;

    /// ends a previously started forced diagnostic capture,
    /// returns true on success
    virtual bool endForcedDiagnosticCapture() = 0;

    //
    // GPU info interface
    //

    /// queries info regarding CPU/GPU clock (timestamp) synchronization
    /// NOTE: Very expensive on vulkan! Do not call every frame
    virtual clock_synchronization_info getClockSynchronizationInfo() = 0;

    /// returns the frequency of GPU timestamps in Hz (seconds = timestamp_delta / getGPUTimestampFrequency())
    virtual uint64_t getGPUTimestampFrequency() const = 0;

    virtual bool isRaytracingEnabled() const = 0;

    virtual backend_type getBackendType() const = 0;

    virtual gpu_info const& getGPUInfo() const = 0;

    //
    // Non-virtual utility
    //

    /// free multiple handles of different types
    /// convenience, for more efficiency use freeRange
    template <class... Args>
    void freeVariadic(Args... handles)
    {
        (this->free(handles), ...);
    }


    /// create a 1D, 2D or 3D texture, or a 1D/2D array
    /// if mips is 0, the maximum amount will be used
    /// if the texture will be used as a UAV, allow_uav must be true
    [[nodiscard]] handle::resource createTexture(phi::format format,
                                                 tg::isize2 size,
                                                 uint32_t mips,
                                                 texture_dimension dim = texture_dimension::t2d,
                                                 uint32_t depth_or_array_size = 1,
                                                 bool allow_uav = false,
                                                 char const* debug_name = nullptr);


    /// create a [multisampled] 2D render- or depth-stencil target
    [[nodiscard]] handle::resource createRenderTarget(phi::format format,
                                                      tg::isize2 size,
                                                      uint32_t samples = 1,
                                                      uint32_t array_size = 1,
                                                      rt_clear_value const* optimized_clear_val = nullptr,
                                                      char const* debug_name = nullptr);


    /// create a buffer with optional element stride, allocation on an upload/readback heap, or allowing UAV access
    [[nodiscard]] handle::resource createBuffer(
        uint32_t size_bytes, uint32_t stride_bytes = 0, resource_heap heap = resource_heap::gpu, bool allow_uav = false, char const* debug_name = nullptr);

    /// create a buffer with optional element stride on resource_heap::upload (shorthand function)
    [[nodiscard]] handle::resource createUploadBuffer(uint32_t size_bytes, uint32_t stride_bytes = 0, char const* debug_name = nullptr);

    [[nodiscard]] handle::resource createResourceFromInfo(arg::resource_description const& info, char const* debug_name = nullptr);

    [[nodiscard]] handle::pipeline_state createComputePipelineState(arg::shader_arg_shapes arg_shapes, arg::shader_binary shader, bool hasRootConsts = false);

    Backend(Backend const&) = delete;
    Backend(Backend&&) = delete;
    Backend& operator=(Backend const&) = delete;
    Backend& operator=(Backend&&) = delete;
    virtual ~Backend() = default;

protected:
    Backend() = default;
};
} // namespace phi
