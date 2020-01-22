#include "verify.hh"
#ifdef PR_BACKEND_VULKAN

#include <cstdio>
#include <cstdlib>
#include <string>

#include <clean-core/assert.hh>

namespace
{
#define CASE_STRINGIFY_RETURN(_val_) \
    case _val_:                      \
        return #_val_

char const* get_vk_error_literal(VkResult vr)
{
    switch (vr)
    {
        CASE_STRINGIFY_RETURN(VK_NOT_READY);
        CASE_STRINGIFY_RETURN(VK_TIMEOUT);
        CASE_STRINGIFY_RETURN(VK_EVENT_SET);
        CASE_STRINGIFY_RETURN(VK_EVENT_RESET);

        CASE_STRINGIFY_RETURN(VK_INCOMPLETE);
        CASE_STRINGIFY_RETURN(VK_ERROR_OUT_OF_HOST_MEMORY);
        CASE_STRINGIFY_RETURN(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        CASE_STRINGIFY_RETURN(VK_ERROR_INITIALIZATION_FAILED);
        CASE_STRINGIFY_RETURN(VK_ERROR_DEVICE_LOST);
        CASE_STRINGIFY_RETURN(VK_ERROR_MEMORY_MAP_FAILED);
        CASE_STRINGIFY_RETURN(VK_ERROR_LAYER_NOT_PRESENT);
        CASE_STRINGIFY_RETURN(VK_ERROR_EXTENSION_NOT_PRESENT);
        CASE_STRINGIFY_RETURN(VK_ERROR_FEATURE_NOT_PRESENT);
        CASE_STRINGIFY_RETURN(VK_ERROR_INCOMPATIBLE_DRIVER);
        CASE_STRINGIFY_RETURN(VK_ERROR_TOO_MANY_OBJECTS);
        CASE_STRINGIFY_RETURN(VK_ERROR_FORMAT_NOT_SUPPORTED);
        CASE_STRINGIFY_RETURN(VK_ERROR_SURFACE_LOST_KHR);
        CASE_STRINGIFY_RETURN(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
        CASE_STRINGIFY_RETURN(VK_SUBOPTIMAL_KHR);
        CASE_STRINGIFY_RETURN(VK_ERROR_OUT_OF_DATE_KHR);
        CASE_STRINGIFY_RETURN(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
        CASE_STRINGIFY_RETURN(VK_ERROR_VALIDATION_FAILED_EXT);
#if VK_HEADER_VERSION >= 13
        CASE_STRINGIFY_RETURN(VK_ERROR_INVALID_SHADER_NV);
#endif
#if VK_HEADER_VERSION >= 24
        CASE_STRINGIFY_RETURN(VK_ERROR_FRAGMENTED_POOL);
#endif
#if VK_HEADER_VERSION >= 39
        CASE_STRINGIFY_RETURN(VK_ERROR_OUT_OF_POOL_MEMORY_KHR);
#endif
#if VK_HEADER_VERSION >= 65
        CASE_STRINGIFY_RETURN(VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR);
        CASE_STRINGIFY_RETURN(VK_ERROR_NOT_PERMITTED_EXT);
#endif
    default:
        return "Unknown VkResult value"; // There are various EXT errors not handled here as well
    }
}

#undef CASE_STRINGIFY_RETURN
}


void pr::backend::vk::detail::verify_failure_handler(VkResult vr, const char* expression, const char* filename, int line)
{
    // Make sure this really is a failed VkResult
    CC_ASSERT(vr != VK_SUCCESS);

    auto const error_string = get_vk_error_literal(vr);

    // TODO: Proper logging
    fprintf(stderr, "[pr][backend][vk] backend verify on `%s' failed.\n", expression);
    fprintf(stderr, "  error: %s\n", error_string);
    fprintf(stderr, "  file %s:%d\n", filename, line);
    fflush(stderr);

    // TODO: Graceful shutdown, possibly memory dump n
    std::abort();
}

#endif
