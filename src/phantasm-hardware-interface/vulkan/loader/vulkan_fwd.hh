#pragma once

#include <cstdint>

extern "C"
{
    typedef uint32_t VkFlags;
    typedef uint32_t VkBool32;
    typedef uint32_t VkSampleMask;
    typedef uint64_t VkDeviceSize;

#define PHI_VK_DEFINE_HANDLE(object) typedef struct object##_T* object

    PHI_VK_DEFINE_HANDLE(VkInstance);
    PHI_VK_DEFINE_HANDLE(VkPhysicalDevice);
    PHI_VK_DEFINE_HANDLE(VkDevice);
    PHI_VK_DEFINE_HANDLE(VkQueue);
    PHI_VK_DEFINE_HANDLE(VkCommandBuffer);
    PHI_VK_DEFINE_HANDLE(VkSurfaceKHR);
    PHI_VK_DEFINE_HANDLE(VkSwapchainKHR);
    PHI_VK_DEFINE_HANDLE(VkRenderPass);
    PHI_VK_DEFINE_HANDLE(VkCommandPool);
    PHI_VK_DEFINE_HANDLE(VkFence);
    PHI_VK_DEFINE_HANDLE(VkEvent);
    PHI_VK_DEFINE_HANDLE(VkSemaphore);

#undef PHI_VK_DEFINE_HANDLE
}

#if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__)) || defined(_M_X64) || defined(__ia64) || defined(_M_IA64) \
    || defined(__aarch64__) || defined(__powerpc64__)
// all is good
#else
#error "Unsupported platform"
#endif
