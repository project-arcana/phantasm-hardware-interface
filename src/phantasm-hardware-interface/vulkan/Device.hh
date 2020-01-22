#pragma once

#include <clean-core/array.hh>

#include <phantasm-hardware-interface/fwd.hh>

#include "loader/volk.hh"

namespace pr::backend::vk
{
struct vulkan_gpu_info;

class Device
{ // reference type
public:
    Device(Device const&) = delete;
    Device(Device&&) noexcept = delete;
    Device& operator=(Device const&) = delete;
    Device& operator=(Device&&) noexcept = delete;
    Device() = default;

    void initialize(vulkan_gpu_info const& device, backend_config const& config);
    void destroy();

    VkQueue getQueueDirect() const { return mQueueDirect; }
    VkQueue getQueueCompute() const { return mQueueCompute; }
    VkQueue getQueueCopy() const { return mQueueCopy; }

    int getQueueFamilyDirect() const { return mQueueFamilies.direct; }
    int getQueueFamilyCompute() const { return mQueueFamilies.compute; }
    int getQueueFamilyCopy() const { return mQueueFamilies.copy; }

public:
    VkPhysicalDeviceMemoryProperties const& getMemoryProperties() const { return mInformation.memory_properties; }
    VkPhysicalDeviceProperties const& getDeviceProperties() const { return mInformation.device_properties; }
    bool hasRaytracing() const { return mHasRaytracing; }

public:
    VkPhysicalDevice getPhysicalDevice() const { return mPhysicalDevice; }
    VkDevice getDevice() const { return mDevice; }

private:
    void initializeRaytracing();

private:
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice = nullptr;

    VkQueue mQueueDirect = nullptr;
    VkQueue mQueueCompute = nullptr;
    VkQueue mQueueCopy = nullptr;

    struct
    {
        int direct = -1;
        int compute = -1;
        int copy = -1;
    } mQueueFamilies;

    // Miscellaneous info
    struct
    {
        VkPhysicalDeviceMemoryProperties memory_properties;
        VkPhysicalDeviceProperties device_properties;
        VkPhysicalDeviceRayTracingPropertiesNV raytrace_properties;
    } mInformation;

    bool mHasRaytracing = false;
};
}
