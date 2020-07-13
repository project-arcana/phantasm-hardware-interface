#pragma once

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/fwd.hh>

#include "loader/vulkan_fwd.hh"

namespace phi::vk
{
[[nodiscard]] VkSurfaceKHR create_platform_surface(VkInstance instance, window_handle const& window_handle);

[[nodiscard]] cc::span<char const* const> get_platform_instance_extensions();

[[nodiscard]] bool can_queue_family_present_on_platform(VkPhysicalDevice physical, uint32_t queue_family_index);

[[nodiscard]] bool can_queue_family_present_on_surface(VkPhysicalDevice physical, uint32_t queue_family_index, VkSurfaceKHR surface);
}
