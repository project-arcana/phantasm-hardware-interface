#pragma once

#include <cstdint>

extern "C"
{
    typedef uint32_t VkFlags;
    typedef uint32_t VkBool32;
    typedef uint64_t VkDeviceSize;
    typedef uint32_t VkSampleMask;

#define PR_VK_DEFINE_HANDLE(object) typedef struct object##_T* object

    PR_VK_DEFINE_HANDLE(VkInstance);
    PR_VK_DEFINE_HANDLE(VkPhysicalDevice);
    PR_VK_DEFINE_HANDLE(VkDevice);
    PR_VK_DEFINE_HANDLE(VkQueue);
    PR_VK_DEFINE_HANDLE(VkCommandBuffer);
    PR_VK_DEFINE_HANDLE(VkSurfaceKHR);
    PR_VK_DEFINE_HANDLE(VkSwapchainKHR);
    PR_VK_DEFINE_HANDLE(VkRenderPass);
    PR_VK_DEFINE_HANDLE(VkCommandPool);
    PR_VK_DEFINE_HANDLE(VkFence);
    PR_VK_DEFINE_HANDLE(VkEvent);
    PR_VK_DEFINE_HANDLE(VkSemaphore);
    PR_VK_DEFINE_HANDLE(VkAccelerationStructureNV);

#undef PR_VK_DEFINE_HANDLE

    struct VkAccelerationStructureCreateInfoNV;
    struct VkGeometryNV;
}

#if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__)) || defined(_M_X64) || defined(__ia64) || defined(_M_IA64) \
    || defined(__aarch64__) || defined(__powerpc64__)
// all is good
#else
#error "Unsupported platform"
#endif
