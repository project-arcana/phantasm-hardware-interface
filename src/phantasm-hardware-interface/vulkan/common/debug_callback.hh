#pragma once

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace pr::backend::vk::detail
{
VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT type,
                                              VkDebugUtilsMessengerCallbackDataEXT const* callback_data,
                                              void* user_data);

}
