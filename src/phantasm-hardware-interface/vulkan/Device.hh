#pragma once

#include <clean-core/array.hh>

#include <phantasm-hardware-interface/fwd.hh>
#include <phantasm-hardware-interface/vulkan/queue_util.hh>

#include "loader/volk.hh"

namespace phi::vk
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

    // returns the requested type, or a fallback type if unavailable
    queue_type getQueueTypeOrFallback(queue_type request) const { return mFallbackQueueTypes[static_cast<uint8_t>(request)]; }

    // reqturns the vulkan queue of the specified type, or the corresponding fallback queue
    VkQueue getRawQueue(queue_type type) const { return mQueues[static_cast<uint8_t>(type)]; }

    int getQueueFamilyDirect() const { return mQueueIndices.direct.family_index; }
    int getQueueFamilyCompute() const { return mQueueIndices.compute.family_index; }
    int getQueueFamilyCopy() const { return mQueueIndices.copy.family_index; }

public:
    VkPhysicalDeviceMemoryProperties const& getMemoryProperties() const { return mInformation.memory_properties; }
    VkPhysicalDeviceProperties const& getDeviceProperties() const { return mInformation.device_properties; }
    bool hasRaytracing() const { return mHasRaytracing; }
    bool hasConservativeRaster() const { return mHasConservativeRaster; }

public:
    VkPhysicalDevice getPhysicalDevice() const { return mPhysicalDevice; }
    VkDevice getDevice() const { return mDevice; }

private:
    void initializeRaytracing();
    void initializeConservativeRaster();

    void queryDeviceProps2(void* property_obj);

private:
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice = nullptr;

    VkQueue mQueues[3];
    phi::queue_type mFallbackQueueTypes[3];

    chosen_queues mQueueIndices;

    // Miscellaneous info
    struct
    {
        VkPhysicalDeviceMemoryProperties memory_properties;
        VkPhysicalDeviceProperties device_properties;
        VkPhysicalDeviceRayTracingPropertiesNV raytrace_properties;
        VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservative_raster_properties;
    } mInformation;

    bool mHasRaytracing = false;
    bool mHasConservativeRaster = false;
    void queryDeviceProps2();
};
}
