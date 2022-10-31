#include "gpu_choice_util.hh"

#include <cstdint>

#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/limits.hh>

#include "common/verify.hh"
#include "queue_util.hh"

namespace
{
[[nodiscard]] bool test_device_properties(VkPhysicalDeviceProperties const& props)
{
    if (props.limits.maxBoundDescriptorSets < phi::limits::max_shader_arguments * 2)
    {
        PHI_LOG_TRACE("GPU {} is unsuitable: Only supports {} max bound descriptor states ({} required)", props.deviceName,
                      props.limits.maxBoundDescriptorSets, phi::limits::max_shader_arguments * 2);
        return false;
    }
    if (props.limits.maxColorAttachments < phi::limits::max_render_targets)
    {
        PHI_LOG_TRACE("GPU {} is unsuitable: Only supports {} max render targets ({} required)", props.deviceName, props.limits.maxColorAttachments,
                      phi::limits::max_render_targets);
        return false;
    }
    if (props.apiVersion < VK_VERSION_1_1)
    {
        PHI_LOG_TRACE("GPU {} is unsuitable: Only supports Vulkan version {} ({} required)", props.deviceName, props.apiVersion, VK_VERSION_1_1);
        return false;
    }
    return true;
}
} // namespace

cc::alloc_array<VkPhysicalDevice> phi::vk::get_physical_devices(VkInstance instance, cc::allocator* alloc)
{
    uint32_t num_physical_devices;
    PHI_VK_VERIFY_NONERROR(vkEnumeratePhysicalDevices(instance, &num_physical_devices, nullptr));
    cc::alloc_array<VkPhysicalDevice> res(num_physical_devices, alloc);
    PHI_VK_VERIFY_NONERROR(vkEnumeratePhysicalDevices(instance, &num_physical_devices, res.data()));
    return res;
}

phi::vk::vulkan_gpu_info phi::vk::get_vulkan_gpu_info(VkPhysicalDevice device, cc::allocator* alloc)
{
    vulkan_gpu_info res;
    res.physical_device = device;
    res.is_suitable = true;
    vkGetPhysicalDeviceProperties(device, &res.physical_device_props);

    // queue capability
    res.queues = get_suitable_queues(device, alloc);
    if (!res.queues.has_direct_queue)
    {
        PHI_LOG_TRACE("GPU {} is unsuitable: Has no direct Queue", res.physical_device_props.deviceName);
        res.is_suitable = false;
    }

    res.available_layers_extensions = getAvailableDeviceExtensions(device, alloc);

    // swapchain extensions
    if (!res.available_layers_extensions.extensions.contains(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
    {
        PHI_LOG_TRACE("GPU {} is unsuitable: Has no Swapchain extension", res.physical_device_props.deviceName);
        res.is_suitable = false;
    }

    // device properties
    if (!test_device_properties(res.physical_device_props))
    {
        res.is_suitable = false;
    }

    // required features
    {
        physical_device_feature_bundle feat_bundle;
        vkGetPhysicalDeviceFeatures2(device, feat_bundle.get());

        // always require GBV features right now (second arg)
        auto const has_required_features = set_or_test_device_features(feat_bundle.get(), true, true, res.physical_device_props.deviceName);
        if (!has_required_features)
        {
            res.is_suitable = false;
        }
    }

    // other queries
    {
        vkGetPhysicalDeviceMemoryProperties(device, &res.mem_props);
    }

    return res;
}

cc::alloc_array<phi::vk::vulkan_gpu_info> phi::vk::get_all_vulkan_gpu_infos(VkInstance instance, cc::allocator* alloc)
{
    auto const physical_devices = get_physical_devices(instance, alloc);

    cc::alloc_array<vulkan_gpu_info> res(physical_devices.size(), alloc);

    for (auto i = 0u; i < physical_devices.size(); ++i)
    {
        res[i] = get_vulkan_gpu_info(physical_devices[i], alloc);
    }
    return res;
}

phi::vk::backbuffer_information phi::vk::get_backbuffer_information(VkPhysicalDevice device, VkSurfaceKHR surface, cc::allocator* alloc)
{
    backbuffer_information res;

    uint32_t num_formats;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, nullptr);
    CC_ASSERT(num_formats != 0);
    res.backbuffer_formats = cc::alloc_array<VkSurfaceFormatKHR>::uninitialized(num_formats, alloc);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, res.backbuffer_formats.data());

    uint32_t num_present_modes;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_present_modes, nullptr);
    CC_ASSERT(num_present_modes != 0);
    res.present_modes = cc::alloc_array<VkPresentModeKHR>::uninitialized(num_present_modes, alloc);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_present_modes, res.present_modes.data());

    return res;
}

VkSurfaceCapabilitiesKHR phi::vk::get_surface_capabilities(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t present_queue_family_index)
{
    // NOTE: we do not technically care about this call, it's purely a sanity check and validation warns if we omit it
    // instead we use the vkGetPhysicalDevice<PLATFORM>PresentationSupportKHR call, which is surface-independent
    VkBool32 can_present = false;
    PHI_VK_VERIFY_SUCCESS(vkGetPhysicalDeviceSurfaceSupportKHR(device, present_queue_family_index, surface, &can_present));
    CC_RUNTIME_ASSERT(can_present && "cannot present on this surface with the current direct queue, contact maintainers");
    VkSurfaceCapabilitiesKHR res;
    PHI_VK_VERIFY_NONERROR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &res));
    return res;
}

VkSurfaceFormatKHR phi::vk::choose_backbuffer_format(cc::span<const VkSurfaceFormatKHR> available_formats, phi::format preference)
{
    if (preference != format::none)
    {
        auto const nativePreference = util::to_vk_format(preference);

        for (auto const& f : available_formats)
            if (f.format == nativePreference)
                return f;
    }

    for (auto const& f : available_formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;

    return available_formats[0];
}

VkPresentModeKHR phi::vk::choose_present_mode(cc::span<const VkPresentModeKHR> available_modes, present_mode mode)
{
    VkPresentModeKHR preferred;
    switch (mode)
    {
    case present_mode::unsynced:
        preferred = VK_PRESENT_MODE_MAILBOX_KHR;
        break;
    case present_mode::unsynced_allow_tearing:
        preferred = VK_PRESENT_MODE_IMMEDIATE_KHR;
        break;
    case present_mode::synced_2nd_vblank: // NOTE: unsupported (so far)
    case present_mode::synced:
        preferred = VK_PRESENT_MODE_FIFO_KHR;
        break;
    }

    for (auto const& m : available_modes)
        if (m == preferred)
            return m;

    // This mode is always available
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D phi::vk::get_swap_extent(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D extent_hint)
{
    if (caps.currentExtent.width != UINT32_MAX)
    {
        return caps.currentExtent;
    }
    else
    {
        // Return the hint, clamped to the min/max extents
        extent_hint.width = cc::clamp(extent_hint.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent_hint.height = cc::clamp(extent_hint.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return extent_hint;
    }
}

cc::alloc_vector<phi::gpu_info> phi::vk::get_available_gpus(cc::span<const vulkan_gpu_info> vk_gpu_infos, cc::allocator* alloc)
{
    cc::alloc_vector<gpu_info> res;
    res.reset_reserve(alloc, vk_gpu_infos.size());

    for (auto i = 0u; i < vk_gpu_infos.size(); ++i)
    {
        auto const& ll_info = vk_gpu_infos[i];
        if (!ll_info.is_suitable)
        {
            continue;
        }

        auto& new_gpu = res.emplace_back_stable();
        new_gpu.index = i;
        new_gpu.vendor = getGPUVendorFromPCIeID(ll_info.physical_device_props.vendorID);
        std::snprintf(new_gpu.name, sizeof(new_gpu.name), "%s", ll_info.physical_device_props.deviceName);

        // new_gpu.has_raytracing = ll_info.available_layers_extensions.extensions.contains(VK_NV_RAY_TRACING_EXTENSION_NAME);
        new_gpu.dedicated_video_memory_bytes = 0;
        new_gpu.dedicated_system_memory_bytes = 0;
        new_gpu.shared_system_memory_bytes = 0;

        for (auto i = 0u; i < ll_info.mem_props.memoryHeapCount; ++i)
        {
            auto const& heap = ll_info.mem_props.memoryHeaps[i];

            if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT && ll_info.physical_device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                new_gpu.dedicated_video_memory_bytes += heap.size;
            }
            else
            {
                new_gpu.shared_system_memory_bytes += heap.size;
            }
        }
    }

    return res;
}

bool phi::vk::set_or_test_device_features(VkPhysicalDeviceFeatures2* arg, bool enable_gbv, bool test_mode, const char* gpu_name_for_logging)
{
    // a single place to both test for existing features and set the features required


    // verify and unfold pNext chain
    CC_ASSERT(arg->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 && "sType for main argument wrong");
    CC_ASSERT(arg->pNext != nullptr && "pNext chain not long enough");

    VkPhysicalDeviceTimelineSemaphoreFeatures& p_next_chain_1 = *static_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(arg->pNext);
    CC_ASSERT(p_next_chain_1.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES && "pNext chain ordered unexpectedly");
    CC_ASSERT(p_next_chain_1.pNext != nullptr && "pNext chain not long enough");
    VkPhysicalDeviceDescriptorIndexingFeatures& p_next_chain_2 = *static_cast<VkPhysicalDeviceDescriptorIndexingFeatures*>(p_next_chain_1.pNext);
    CC_ASSERT(p_next_chain_2.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES && "pNext chain ordered unexpectedly");

#define PHI_VK_SET_OR_TEST_PROPERTY(_prop_, _prop_name_)                                                                           \
    if (test_mode)                                                                                                                 \
    {                                                                                                                              \
        if (_prop_ != VK_TRUE)                                                                                                     \
        {                                                                                                                          \
            if (gpu_name_for_logging)                                                                                              \
            {                                                                                                                      \
                PHI_LOG_TRACE("GPU {} is unsuitable: Device feature \"" #_prop_name_ "\" is not supported", gpu_name_for_logging); \
            }                                                                                                                      \
            return false;                                                                                                          \
        }                                                                                                                          \
    }                                                                                                                              \
    else                                                                                                                           \
    {                                                                                                                              \
        _prop_ = VK_TRUE;                                                                                                          \
    }                                                                                                                              \
    (void)0

#define PHI_VK_SET_OR_TEST(_feat_) PHI_VK_SET_OR_TEST_PROPERTY(arg->features._feat_, _feat_)


    // == set and test features ==

    // added by discovered necessity
    PHI_VK_SET_OR_TEST(samplerAnisotropy);
    PHI_VK_SET_OR_TEST(geometryShader);

    // 100% support
    PHI_VK_SET_OR_TEST(fillModeNonSolid);
    PHI_VK_SET_OR_TEST(fragmentStoresAndAtomics);
    PHI_VK_SET_OR_TEST(independentBlend);
    PHI_VK_SET_OR_TEST(robustBufferAccess);

    // > 98% support
    PHI_VK_SET_OR_TEST(drawIndirectFirstInstance);
    PHI_VK_SET_OR_TEST(fullDrawIndexUint32);
    PHI_VK_SET_OR_TEST(vertexPipelineStoresAndAtomics);
    PHI_VK_SET_OR_TEST(imageCubeArray);
    PHI_VK_SET_OR_TEST(multiDrawIndirect);
    PHI_VK_SET_OR_TEST(shaderClipDistance);
    PHI_VK_SET_OR_TEST(shaderCullDistance);
    PHI_VK_SET_OR_TEST(dualSrcBlend);
    PHI_VK_SET_OR_TEST(largePoints);
    PHI_VK_SET_OR_TEST(logicOp);
    PHI_VK_SET_OR_TEST(multiViewport);
    PHI_VK_SET_OR_TEST(occlusionQueryPrecise);
    PHI_VK_SET_OR_TEST(shaderSampledImageArrayDynamicIndexing);
    PHI_VK_SET_OR_TEST(shaderStorageBufferArrayDynamicIndexing);
    PHI_VK_SET_OR_TEST(shaderStorageImageArrayDynamicIndexing);
    PHI_VK_SET_OR_TEST(shaderStorageImageWriteWithoutFormat);
    PHI_VK_SET_OR_TEST(shaderTessellationAndGeometryPointSize);
    PHI_VK_SET_OR_TEST(shaderUniformBufferArrayDynamicIndexing);
    PHI_VK_SET_OR_TEST(textureCompressionBC);
    PHI_VK_SET_OR_TEST(wideLines);
    PHI_VK_SET_OR_TEST(depthBiasClamp);
    PHI_VK_SET_OR_TEST(depthClamp);
    PHI_VK_SET_OR_TEST(variableMultisampleRate);
    PHI_VK_SET_OR_TEST(inheritedQueries);
    PHI_VK_SET_OR_TEST(pipelineStatisticsQuery);
    PHI_VK_SET_OR_TEST(sampleRateShading);
    PHI_VK_SET_OR_TEST(shaderImageGatherExtended);
    PHI_VK_SET_OR_TEST(shaderStorageImageExtendedFormats);
    PHI_VK_SET_OR_TEST(tessellationShader);


    if (enable_gbv)
    {
        // features required for GBV
        PHI_VK_SET_OR_TEST(fragmentStoresAndAtomics);
        PHI_VK_SET_OR_TEST(vertexPipelineStoresAndAtomics);
    }

    // timeline semaphores (hard requirement for fence API)
    PHI_VK_SET_OR_TEST_PROPERTY(p_next_chain_1.timelineSemaphore, timelineSemaphore);

    // dynamic descriptor indexing (required for empty shader view API / "bindless", currently hard requirement)
    PHI_VK_SET_OR_TEST_PROPERTY(p_next_chain_2.shaderSampledImageArrayNonUniformIndexing, shaderSampledImageArrayNonUniformIndexing);
    PHI_VK_SET_OR_TEST_PROPERTY(p_next_chain_2.runtimeDescriptorArray, runtimeDescriptorArray);
    PHI_VK_SET_OR_TEST_PROPERTY(p_next_chain_2.descriptorBindingVariableDescriptorCount, descriptorBindingVariableDescriptorCount);
    PHI_VK_SET_OR_TEST_PROPERTY(p_next_chain_2.descriptorBindingPartiallyBound, descriptorBindingPartiallyBound);

    return true;

#undef PHI_VK_SET_OR_TEST
#undef PHI_VK_SET_OR_TEST_PROPERTY
}
