#pragma once

#include <cstdint>

#include <clean-core/fwd.hh>

namespace phi
{
enum class adapter_preference : uint8_t
{
    // use the GPU with the highest VRAM
    highest_vram,

    // use the first GPU found by the API
    first,

    // prefer integrated GPUs (like Intel Graphics)
    integrated,

    // use the GPU with the highest feature level
    highest_feature_level,

    // use the n-th GPU, n given by the explicit_adapter_index field
    explicit_index
};

enum class validation_level : uint8_t
{
    // No validation, fastest
    off,

    // D3D12: Whether to enable debug layers, requires installed D3D12 SDK
    // Vulkan: Whether to enable validation, requires installed LunarG SDK
    on,

    // D3D12: Whether to additionally enable GPU based validation (slow)
    //
    // Vulkan: Whether to additionally enable LunarG GPU-assisted validation (slow)
    //          Requires a reserved descriptor set, can fail if the device only supports 8, like some iGPUs.
    //
    // Can prevent diagnostic tools like Renderdoc and NSight from working properly,
    // but the backend will attempt to auto-disable if those are detected
    on_extended,

    // D3D12: Whether to additionally enable DRED (Device Removed Extended Data)
    //          with automatic breadcrumbs and pagefault recovery (very slow)
    //          see: https://docs.microsoft.com/en-us/windows/win32/direct3d12/use-dred
    //
    // Vulkan: same as on_extended
    on_extended_dred
};

struct backend_config
{
    /// whether to enable API-level validations
    validation_level validation = validation_level::off;

    /// the strategy for choosing a physical GPU
    adapter_preference adapter = adapter_preference::highest_vram;
    uint32_t explicit_adapter_index = uint32_t(-1);

    enum native_feature_flags : uint8_t
    {
        native_feature_none = 0,

        // Vulkan: Dump all Vulkan API calls in text form
        native_feature_vk_api_dump = 1 << 0,

        // D3D12: Cause a breakpoint on any warning, useful to find it's source
        // for an equivalent Vulkan feature, set a breakpoint in <phi>/vulkan/common/debug_callback.cc
        native_feature_d3d12_break_on_warn = 1 << 1,

        // D3D12: skip destroying ID3D12Device on shutdown to avoid a known crash in Windows pre-21H1 with enabled GPU based validation
        // this causes a lot of spam on shutdown because of live COM objects, but will avoid the crash
        native_feature_d3d12_workaround_device_release_crash = 1 << 2,

        // Vulkan: present from the discrete compute queue (instead of the default direct queue)
        native_feature_vk_present_from_compute = 1 << 3,

        // Vulkan: Enable the best practices validation layer (VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT)
        // has proved to be of questionable reliability, requires at least validation_level::on
        native_feature_vk_best_practices_layer = 1 << 4
    };

    /// native features to enable
    uint8_t native_features = native_feature_none;

    /// whether to enable DXR / VK raytracing features if available
    bool enable_raytracing = true;

    /// whether to print basic information on init
    bool print_startup_message = true;

    /// amount of threads to accomodate
    /// backend calls must only be made from <= [num_threads] unique OS threads
    uint32_t num_threads = 1;

    /// allocator for init-time allocations, only hit during init and shutdown
    cc::allocator* static_allocator = cc::system_allocator;
    /// allocator for runtime allocations, must be thread-safe
    cc::allocator* dynamic_allocator = cc::system_allocator;

    // resource limits
    uint32_t max_num_swapchains = 32;
    uint32_t max_num_resources = 2048;
    uint32_t max_num_pipeline_states = 1024;
    uint32_t max_num_cbvs = 2048;
    uint32_t max_num_srvs = 2048;
    uint32_t max_num_uavs = 2048;
    uint32_t max_num_samplers = 1024;
    uint32_t max_num_fences = 4096;
    uint32_t max_num_accel_structs = 2048;
    uint32_t max_num_raytrace_pipeline_states = 256;

    // command list allocator sizes (total = #threads * #allocs/thread * #lists/alloc)
    uint32_t num_direct_cmdlist_allocators_per_thread = 5;
    uint32_t num_direct_cmdlists_per_allocator = 5;
    uint32_t num_compute_cmdlist_allocators_per_thread = 5;
    uint32_t num_compute_cmdlists_per_allocator = 5;
    uint32_t num_copy_cmdlist_allocators_per_thread = 3;
    uint32_t num_copy_cmdlists_per_allocator = 3;

    // command list limits
    uint32_t max_num_unique_transitions_per_cmdlist = 64;

    // query heap sizes
    uint32_t num_timestamp_queries = 1024;
    uint32_t num_occlusion_queries = 1024;
    uint32_t num_pipeline_stat_queries = 256;
};
}
