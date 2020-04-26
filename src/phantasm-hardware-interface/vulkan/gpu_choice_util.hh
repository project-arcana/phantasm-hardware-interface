#pragma once

#include <clean-core/array.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/gpu_info.hh>
#include <phantasm-hardware-interface/types.hh>

#include "layer_extension_util.hh"
#include "loader/volk.hh"
#include "queue_util.hh"

namespace phi::vk
{
struct vulkan_gpu_info
{
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_props;
    VkPhysicalDeviceMemoryProperties mem_props;
    cc::array<VkSurfaceFormatKHR> backbuffer_formats;
    cc::array<VkPresentModeKHR> present_modes;
    VkSurfaceCapabilitiesKHR surface_capabilities;
    lay_ext_set available_layers_extensions;
    suitable_queues queues;

    bool is_suitable = false;
};

struct backbuffer_information
{
    cc::array<VkSurfaceFormatKHR> backbuffer_formats;
    cc::array<VkPresentModeKHR> present_modes;
};

/// correctly initialized bundle of VkPhysicalDeviceXX structs for usage during feature tests and
struct physical_device_feature_bundle
{
    VkPhysicalDeviceFeatures2 features = {};
    VkPhysicalDeviceTimelineSemaphoreFeatures features_time_sem = {};

    physical_device_feature_bundle()
    {
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features_time_sem;
        features_time_sem.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    }
    VkPhysicalDeviceFeatures2* get() { return &features; }
};

bool set_or_test_device_features(VkPhysicalDeviceFeatures2* arg, bool enable_gbv, bool test_mode);

/// receive all physical devices visible to the instance
[[nodiscard]] cc::array<VkPhysicalDevice> get_physical_devices(VkInstance instance);

/// receive full information about a GPU, relatively slow
[[nodiscard]] vulkan_gpu_info get_vulkan_gpu_info(VkPhysicalDevice device, VkSurfaceKHR surface);

[[nodiscard]] cc::array<vulkan_gpu_info> get_all_vulkan_gpu_infos(VkInstance instance, VkSurfaceKHR surface);

[[nodiscard]] cc::vector<gpu_info> get_available_gpus(cc::span<vulkan_gpu_info const> vk_gpu_infos);


/// receive only backbuffer-related information
[[nodiscard]] backbuffer_information get_backbuffer_information(VkPhysicalDevice device, VkSurfaceKHR surface);

/// receive surface capabilities
[[nodiscard]] VkSurfaceCapabilitiesKHR get_surface_capabilities(VkPhysicalDevice device, VkSurfaceKHR surface);

[[nodiscard]] VkSurfaceFormatKHR choose_backbuffer_format(cc::span<VkSurfaceFormatKHR const> available_formats);
[[nodiscard]] VkPresentModeKHR choose_present_mode(cc::span<VkPresentModeKHR const> available_modes, present_mode mode);

[[nodiscard]] VkExtent2D get_swap_extent(VkSurfaceCapabilitiesKHR const& capabilities, VkExtent2D extent_hint);

[[nodiscard]] inline VkSurfaceTransformFlagBitsKHR choose_identity_transform(VkSurfaceCapabilitiesKHR const& capabilities)
{
    return (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
}

[[nodiscard]] inline VkCompositeAlphaFlagBitsKHR choose_alpha_mode(VkSurfaceCapabilitiesKHR const& capabilities)
{
    for (auto flag : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
        if (capabilities.supportedCompositeAlpha & flag)
            return flag;

    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

inline bool memory_type_from_properties(VkPhysicalDeviceMemoryProperties const& memory_properties, uint32_t type_bits, VkFlags requirements_mask, uint32_t& out_type_index)
{
    // Search memtypes to find first index with those properties
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
    {
        if ((type_bits & 1) == 1)
        {
            // Type is available, does it match user properties?
            if ((memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask)
            {
                out_type_index = i;
                return true;
            }
        }
        type_bits >>= 1;
    }
    // No memory types matched, return failure
    return false;
}

}
