#pragma once

#include <clean-core/alloc_array.hh>
#include <clean-core/alloc_vector.hh>
#include <clean-core/span.hh>

#include <phantasm-hardware-interface/features/gpu_info.hh>
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
    LayerExtensionSet available_layers_extensions;
    suitable_queues queues;

    bool is_suitable = false;
};

struct backbuffer_information
{
    cc::alloc_array<VkSurfaceFormatKHR> backbuffer_formats;
    cc::alloc_array<VkPresentModeKHR> present_modes;
};

/// correctly initialized bundle of VkPhysicalDeviceXX structs for usage during feature tests and
struct physical_device_feature_bundle
{
    VkPhysicalDeviceFeatures2 features = {};
    VkPhysicalDeviceTimelineSemaphoreFeatures features_time_sem = {};
    VkPhysicalDeviceDescriptorIndexingFeatures features_descriptor_indexing = {};

    physical_device_feature_bundle()
    {
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features_time_sem;
        features_time_sem.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        features_time_sem.pNext = &features_descriptor_indexing;
        features_descriptor_indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    }
    VkPhysicalDeviceFeatures2* get() { return &features; }

    physical_device_feature_bundle(physical_device_feature_bundle const&) = delete;
    physical_device_feature_bundle(physical_device_feature_bundle&&) = delete;
};

bool set_or_test_device_features(VkPhysicalDeviceFeatures2* arg, bool enable_gbv, bool test_mode, char const* gpu_name_for_logging = nullptr);

/// receive all physical devices visible to the instance
cc::alloc_array<VkPhysicalDevice> get_physical_devices(VkInstance instance, cc::allocator* alloc);

/// receive full information about a GPU, relatively slow
vulkan_gpu_info get_vulkan_gpu_info(VkPhysicalDevice device, cc::allocator* alloc);

cc::alloc_array<vulkan_gpu_info> get_all_vulkan_gpu_infos(VkInstance instance, cc::allocator* alloc);

cc::alloc_vector<gpu_info> get_available_gpus(cc::span<vulkan_gpu_info const> vk_gpu_infos, cc::allocator* alloc);


/// receive only backbuffer-related information
backbuffer_information get_backbuffer_information(VkPhysicalDevice device, VkSurfaceKHR surface, cc::allocator* alloc);

/// receive surface capabilities
VkSurfaceCapabilitiesKHR get_surface_capabilities(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t present_queue_family_index);

VkSurfaceFormatKHR choose_backbuffer_format(cc::span<VkSurfaceFormatKHR const> available_formats, phi::format preference);

VkPresentModeKHR choose_present_mode(cc::span<VkPresentModeKHR const> available_modes, present_mode mode);

VkExtent2D get_swap_extent(VkSurfaceCapabilitiesKHR const& capabilities, VkExtent2D extent_hint);

inline VkSurfaceTransformFlagBitsKHR choose_identity_transform(VkSurfaceCapabilitiesKHR const& capabilities)
{
    return (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
}

inline VkCompositeAlphaFlagBitsKHR choose_alpha_mode(VkSurfaceCapabilitiesKHR const& capabilities)
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

} // namespace phi::vk
