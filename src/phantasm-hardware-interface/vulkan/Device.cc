#include "Device.hh"

#include <clean-core/assert.hh>
#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/config.hh>

#include "common/verify.hh"
#include "gpu_choice_util.hh"
#include "queue_util.hh"

bool phi::vk::Device::initialize(vulkan_gpu_info const& device, backend_config const& config)
{
    mPhysicalDevice = device.physical_device;
    CC_ASSERT(mDevice == nullptr && "vk::Device initialized twice");

    mHasRaytracing = false;
    mHasConservativeRaster = false;
    auto const active_lay_ext = getUsedDeviceExtensions(device.available_layers_extensions, config, mHasRaytracing, mHasConservativeRaster);

    // chose queues
    mQueueIndices = get_chosen_queues(device.queues);
    if (!mQueueIndices.direct.valid())
    {
        PHI_LOG_ASSERT("Fatal: Failed to find direct queue");
        return false;
    }

    // setup feature struct chain and fill it
    physical_device_feature_bundle feat_bundle;
    set_or_test_device_features(feat_bundle.get(), config.validation >= validation_level::on_extended, false);

    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = feat_bundle.get();
    device_info.enabledExtensionCount = uint32_t(active_lay_ext.extensions.size());
    device_info.ppEnabledExtensionNames = active_lay_ext.extensions.empty() ? nullptr : active_lay_ext.extensions.data();
    device_info.enabledLayerCount = uint32_t(active_lay_ext.layers.size());
    device_info.ppEnabledLayerNames = active_lay_ext.layers.empty() ? nullptr : active_lay_ext.layers.data();

    // assemble queue creation struct
    float const global_queue_priorities[] = {1.f, 1.f, 1.f};
    cc::capped_vector<VkDeviceQueueCreateInfo, 3> queue_create_infos;
    auto const f_add_queue = [&](int family_index) -> void
    {
        for (auto& info : queue_create_infos)
        {
            if (info.queueFamilyIndex == uint32_t(family_index))
            {
                ++info.queueCount;
                return;
            }
        }

        // add new create info
        auto& queue_info = queue_create_infos.emplace_back();
        queue_info = {};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueCount = 1;
        queue_info.queueFamilyIndex = uint32_t(family_index);
        queue_info.pQueuePriorities = global_queue_priorities;
    };

    for (auto const& q_i : {mQueueIndices.direct, mQueueIndices.copy, mQueueIndices.compute})
        if (q_i.valid())
            f_add_queue(q_i.family_index);

    device_info.pQueueCreateInfos = queue_create_infos.data();
    device_info.queueCreateInfoCount = uint32_t(queue_create_infos.size());

    if (vkCreateDevice(mPhysicalDevice, &device_info, nullptr, &mDevice) != VK_SUCCESS)
    {
        PHI_LOG_ASSERT("Fatal: vkCreateDevice call failed");
        mDevice = nullptr;
        return false;
    }

    // Load device-based Vulkan entrypoints
    volkLoadDevice(mDevice);

    // Query ("create") queues
    {
        static_assert(uint8_t(queue_type::direct) == 0, "indices unexpected");
        static_assert(uint8_t(queue_type::compute) == 1, "indices unexpected");
        static_assert(uint8_t(queue_type::copy) == 2, "indices unexpected");

        vkGetDeviceQueue(mDevice, uint32_t(mQueueIndices.direct.family_index), uint32_t(mQueueIndices.direct.queue_index), &mQueues[uint8_t(queue_type::direct)]);
        mFallbackQueueTypes[uint8_t(queue_type::direct)] = queue_type::direct;

        if (mQueueIndices.compute.valid())
        {
            // there is a discrete compute queue,
            vkGetDeviceQueue(mDevice, uint32_t(mQueueIndices.compute.family_index), uint32_t(mQueueIndices.compute.queue_index),
                             &mQueues[uint8_t(queue_type::compute)]);
            mFallbackQueueTypes[uint8_t(queue_type::compute)] = queue_type::compute; // fallback just passing through
        }
        else
        {
            // there is no discrete compute queue, fallback to the direct queue
            mQueues[uint8_t(queue_type::compute)] = mQueues[uint8_t(queue_type::direct)];
            mFallbackQueueTypes[uint8_t(queue_type::compute)] = queue_type::direct;
        }

        if (mQueueIndices.copy.valid())
        {
            // discrete copy
            vkGetDeviceQueue(mDevice, uint32_t(mQueueIndices.copy.family_index), uint32_t(mQueueIndices.copy.queue_index), &mQueues[uint8_t(queue_type::copy)]);
            mFallbackQueueTypes[uint8_t(queue_type::copy)] = queue_type::copy;
        }
        else
        {
            // fallback to direct
            mQueues[uint8_t(queue_type::copy)] = mQueues[uint8_t(queue_type::direct)];
            mFallbackQueueTypes[uint8_t(queue_type::copy)] = queue_type::direct;
        }
    }

    // copy info
    {
        mInformation.memory_properties = device.mem_props;
        mInformation.device_properties = device.physical_device_props;
    }

    if (hasRaytracing())
        initializeRaytracing();

    if (hasConservativeRaster())
        initializeConservativeRaster();

    return true;
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
    queryDeviceProps2(&mInformation.raytrace_properties);
}

void phi::vk::Device::initializeConservativeRaster()
{
    mInformation.conservative_raster_properties = {};
    mInformation.conservative_raster_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT;
    queryDeviceProps2(&mInformation.conservative_raster_properties);
}

void phi::vk::Device::queryDeviceProps2(void* property_obj)
{
    VkPhysicalDeviceProperties2 props = {};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = property_obj;
    props.properties = {};

    vkGetPhysicalDeviceProperties2(mPhysicalDevice, &props);
}
