#pragma once

#include <cstdint>

#include <clean-core/fwd.hh>

namespace phi
{
enum class adapter_preference : uint8_t
{
    highest_vram,
    first,
    integrated,
    highest_feature_level,
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
    // Vulkan: Whether to additionally enable LunarG GPU-assisted validation
    //          Slow, and requires a reserved descriptor set. If your device
    //          only has 8 (max shader args * 2), like an IGP, this could fail
    //
    // Extended validation for both APIs can prevent diagnostic tools like
    // Renderdoc and NSight from working properly (PIX will work though)
    on_extended,

    // D3D12: Whether to additionally enable DRED (Device Removed Extended Data)
    //          with automatic breadcrumbs and pagefault recovery (very slow)
    //          see: https://docs.microsoft.com/en-us/windows/win32/direct3d12/use-dred
    //
    // Vulkan: No additional effect
    on_extended_dred
};

struct backend_config
{
    /// whether to enable API-level validations
    validation_level validation = validation_level::off;

    /// the strategy for choosing a physical GPU
    adapter_preference adapter = adapter_preference::highest_vram;
    unsigned explicit_adapter_index = unsigned(-1);

    enum native_feature_flags : uint8_t
    {
        native_feature_none = 0,
        native_feature_vk_api_dump = 1 << 0,
        native_feature_d3d12_break_on_warn = 1 << 1,
        native_feature_d3d12_workaround_device_release_crash = 1 << 2
    };

    /// native features to enable
    uint8_t native_features = native_feature_none;

    /// whether to enable DXR / VK raytracing features if available
    bool enable_raytracing = true;

    /// whether to print basic information on init
    bool print_startup_message = true;

    /// whether to present from the discrete compute queue (instead of the default direct queue)
    bool present_from_compute_queue = false;

    /// amount of threads to accomodate
    /// backend calls must only be made from <= [num_threads] unique OS threads
    unsigned num_threads = 1;

    /// allocator for init-time allocations, only hit during init and shutdown
    cc::allocator* static_allocator = cc::system_allocator;
    /// allocator for runtime allocations, must be thread-safe
    cc::allocator* dynamic_allocator = cc::system_allocator;

    /// resource limits
    unsigned max_num_swapchains = 32;
    unsigned max_num_resources = 2048;
    unsigned max_num_pipeline_states = 1024;
    unsigned max_num_cbvs = 2048;
    unsigned max_num_srvs = 2048;
    unsigned max_num_uavs = 2048;
    unsigned max_num_samplers = 1024;
    unsigned max_num_fences = 4096;
    unsigned max_num_accel_structs = 2048;
    unsigned max_num_raytrace_pipeline_states = 256;

    /// command list allocator sizes (total = #threads * #allocs/thread * #lists/alloc)
    unsigned num_direct_cmdlist_allocators_per_thread = 5;
    unsigned num_direct_cmdlists_per_allocator = 5;
    unsigned num_compute_cmdlist_allocators_per_thread = 5;
    unsigned num_compute_cmdlists_per_allocator = 5;
    unsigned num_copy_cmdlist_allocators_per_thread = 3;
    unsigned num_copy_cmdlists_per_allocator = 3;

    /// query heap sizes
    unsigned num_timestamp_queries = 1024;
    unsigned num_occlusion_queries = 1024;
    unsigned num_pipeline_stat_queries = 256;
};
}
