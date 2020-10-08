#pragma once

#include <clean-core/macros.hh>

#include <phantasm-hardware-interface/common/value_category.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk::detail
{
[[noreturn]] CC_COLD_FUNC CC_DONT_INLINE void verify_failure_handler(VkResult vr, char const* expression, char const* filename, int line);
}

// TODO: option to disable verify in release builds
// NOTE: possibly merge this somehow with CC_ASSERT

/// Terminates with a detailed error message if the given VkResult lvalue is not VK_SUCCESS
#define PHI_VK_ASSERT_SUCCESS(_val_)                                                                     \
    static_assert(PHI_IS_LVALUE_EXPRESSION(_val_), "Use PHI_VK_VERIFY_SUCCESS for prvalue expressions"); \
    (CC_UNLIKELY(_val_ != VK_SUCCESS) ? ::phi::vk::detail::verify_failure_handler(_val_, #_val_ " is not VK_SUCCESS", __FILE__, __LINE__) : void(0))

/// Terminates with a detailed error message if the given VkResult lvalue is not an error (less strict)
#define PHI_VK_ASSERT_NONERROR(_val_)                                                                     \
    static_assert(PHI_IS_LVALUE_EXPRESSION(_val_), "Use PHI_VK_VERIFY_NONERROR for prvalue expressions"); \
    (CC_UNLIKELY(_val_ < VK_SUCCESS) ? ::phi::vk::detail::verify_failure_handler(_val_, #_val_ " is an error value", __FILE__, __LINE__) : void(0))

/// Executes the given expression and terminates with a detailed error message if the VkResult is not VK_SUCCESS
#define PHI_VK_VERIFY_SUCCESS(_expr_)                                                                     \
    static_assert(PHI_IS_PRVALUE_EXPRESSION(_expr_), "Use PHI_VK_ASSERT_SUCCESS for lvalue expressions"); \
    do                                                                                                    \
    {                                                                                                     \
        ::VkResult const op_res = (_expr_);                                                               \
        if (CC_UNLIKELY(op_res != VK_SUCCESS))                                                            \
        {                                                                                                 \
            ::phi::vk::detail::verify_failure_handler(op_res, #_expr_, __FILE__, __LINE__);               \
        }                                                                                                 \
    } while (0)

/// Executes the given expression and terminates with a detailed error message if the VkResult is not an error (less strict)
#define PHI_VK_VERIFY_NONERROR(_expr_)                                                                     \
    static_assert(PHI_IS_PRVALUE_EXPRESSION(_expr_), "Use PHI_VK_ASSERT_NONERROR for lvalue expressions"); \
    do                                                                                                     \
    {                                                                                                      \
        ::VkResult const op_res = (_expr_);                                                                \
        if (CC_UNLIKELY(op_res < VK_SUCCESS))                                                              \
        {                                                                                                  \
            ::phi::vk::detail::verify_failure_handler(op_res, #_expr_, __FILE__, __LINE__);                \
        }                                                                                                  \
    } while (0)
