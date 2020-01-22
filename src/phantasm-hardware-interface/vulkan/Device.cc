#include "Device.hh"

#include <clean-core/assert.hh>
#include <clean-core/capped_vector.hh>

#include "common/log.hh"
#include "common/verify.hh"
#include "gpu_choice_util.hh"
#include "queue_util.hh"

void phi::vk::Device::initialize(vulkan_gpu_info const& device, backend_config const& config)
{
    mPhysicalDevice = device.physical_device;
    CC_ASSERT(mDevice == nullptr && "vk::Device initialized twice");

    mHasRaytracing = false;
    auto const active_lay_ext = get_used_device_lay_ext(device.available_layers_extensions, config, mHasRaytracing);

    // chose family queue indices
    {
        auto const chosen_queues = get_chosen_queues(device.queues);
        mQueueFamilies.direct = chosen_queues.direct;
        mQueueFamilies.compute = chosen_queues.separate_compute;
        mQueueFamilies.copy = chosen_queues.separate_copy;

        CC_RUNTIME_ASSERT(mQueueFamilies.direct != -1 && "vk::Device failed to find direct queue");
    }

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.enabledExtensionCount = uint32_t(active_lay_ext.extensions.size());
    device_info.ppEnabledExtensionNames = active_lay_ext.extensions.empty() ? nullptr : active_lay_ext.extensions.data();
    device_info.enabledLayerCount = uint32_t(active_lay_ext.layers.size());
    device_info.ppEnabledLayerNames = active_lay_ext.layers.empty() ? nullptr : active_lay_ext.layers.data();

    auto const global_queue_priority = 1.f;
    cc::capped_vector<VkDeviceQueueCreateInfo, 3> queue_create_infos;
    for (auto const q_i : {mQueueFamilies.direct, mQueueFamilies.copy, mQueueFamilies.compute})
    {
        if (q_i == -1)
            continue;

        auto& queue_info = queue_create_infos.emplace_back();
        queue_info = {};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueCount = 1;
        queue_info.queueFamilyIndex = static_cast<uint32_t>(q_i);
        queue_info.pQueuePriorities = &global_queue_priority;
    }

    device_info.pQueueCreateInfos = queue_create_infos.data();
    device_info.queueCreateInfoCount = uint32_t(queue_create_infos.size());

    // TODO
    // NOTE: Also update suitability requirements in gpu_choice_util.cc
    VkPhysicalDeviceFeatures features = {};
    features.samplerAnisotropy = VK_TRUE;
    features.geometryShader = VK_TRUE;
    device_info.pEnabledFeatures = &features;

    PHI_VK_VERIFY_SUCCESS(vkCreateDevice(mPhysicalDevice, &device_info, nullptr, &mDevice));

    volkLoadDevice(mDevice);

    // Query queues
    {
        if (mQueueFamilies.direct != -1)
            vkGetDeviceQueue(mDevice, uint32_t(mQueueFamilies.direct), 0, &mQueueDirect);
        if (mQueueFamilies.compute != -1)
            vkGetDeviceQueue(mDevice, uint32_t(mQueueFamilies.compute), 0, &mQueueCompute);
        if (mQueueFamilies.copy != -1)
            vkGetDeviceQueue(mDevice, uint32_t(mQueueFamilies.copy), 0, &mQueueCopy);
    }

    // copy info
    {
        mInformation.memory_properties = device.mem_props;
        mInformation.device_properties = device.physical_device_props;
    }

    if (hasRaytracing())
    {
        initializeRaytracing();
    }
}

void phi::vk::Device::destroy()
{
    PHI_VK_VERIFY_SUCCESS(vkDeviceWaitIdle(mDevice));
    vkDestroyDevice(mDevice, nullptr);
}

void phi::vk::Device::initializeRaytracing()
{
    mInformation.raytrace_properties = {};
    mInformation.raytrace_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;

    VkPhysicalDeviceProperties2 props = {};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &mInformation.raytrace_properties;
    props.properties = {};

    vkGetPhysicalDeviceProperties2(mPhysicalDevice, &props);
}
