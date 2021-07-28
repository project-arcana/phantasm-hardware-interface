#pragma once

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/fwd.hh>

#include "loader/vulkan_fwd.hh"

namespace phi::vk
{
VkSurfaceKHR create_platform_surface(VkInstance instance, window_handle const& window_handle);

cc::span<char const* const> get_platform_instance_extensions();

bool can_queue_family_present_on_platform(VkPhysicalDevice physical, uint32_t queue_family_index);

bool can_queue_family_present_on_surface(VkPhysicalDevice physical, uint32_t queue_family_index, VkSurfaceKHR surface);
} // namespace phi::vk
