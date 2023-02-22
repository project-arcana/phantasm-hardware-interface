#include "debug_callback.hh"

#include <phantasm-hardware-interface/common/log.hh>

#include <clean-core/breakpoint.hh>

namespace
{
[[maybe_unused]] constexpr char const* to_literal(VkDebugUtilsMessageSeverityFlagBitsEXT severity)
{
    switch (severity)
    {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        return "verbose";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        return "info";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        return "warning";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        return "error";
    default:
        return "unknown severity";
    }
}
[[maybe_unused]] constexpr char const* to_literal(VkDebugUtilsMessageTypeFlagsEXT type)
{
    switch (type)
    {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
        return "general";
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
        return "validation";
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
        return "performance";
    default:
        return "unknown type";
    }
}
} // namespace

VkBool32 phi::vk::detail::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                         VkDebugUtilsMessageTypeFlagsEXT type,
                                         const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                         void* /*user_data*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        (void)type;
        // RICH_LOG_IMPL(phi::detail::vulkan_log)("{} message - {}", to_literal(type), to_literal(severity));
        // RICH_LOG_IMPL(phi::detail::vulkan_log)("{}", callback_data->pMessage);
        PHI_LOG_WARN("Vulkan Message: {}", callback_data->pMessage);
        // cc::breakpoint(); // NOTE: setting a breakpoint here sometimes doesn't suffice, this does
    }
    return VK_FALSE;
}
