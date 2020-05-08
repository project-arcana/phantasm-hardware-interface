#include "gpu_choice_util.hh"

#include <cstdint>

#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/detail/log.hh>
#include <phantasm-hardware-interface/limits.hh>

#include "common/verify.hh"
#include "queue_util.hh"

namespace
{
[[nodiscard]] bool test_device_properties(VkPhysicalDeviceProperties const& props)
{
    if (props.limits.maxBoundDescriptorSets < phi::limits::max_shader_arguments * 2)
        return false;

    if (props.limits.maxColorAttachments < phi::limits::max_render_targets)
        return false;

    if (props.apiVersion < VK_VERSION_1_1)
        return false;

    return true;
}
}

cc::array<VkPhysicalDevice> phi::vk::get_physical_devices(VkInstance instance)
{
    uint32_t num_physical_devices;
    PHI_VK_VERIFY_NONERROR(vkEnumeratePhysicalDevices(instance, &num_physical_devices, nullptr));
    cc::array<VkPhysicalDevice> res(num_physical_devices);
    PHI_VK_VERIFY_NONERROR(vkEnumeratePhysicalDevices(instance, &num_physical_devices, res.data()));
    return res;
}

phi::vk::vulkan_gpu_info phi::vk::get_vulkan_gpu_info(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    vulkan_gpu_info res;
    res.physical_device = device;
    res.is_suitable = true;

    // queue capability
    res.queues = get_suitable_queues(device, surface);
    if (!res.queues.has_direct_queue)
        res.is_suitable = false;

    res.available_layers_extensions = get_available_device_lay_ext(device);

    // swapchain extensions
    if (!res.available_layers_extensions.extensions.contains(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        res.is_suitable = false;


    // device properties
    {
        vkGetPhysicalDeviceProperties(device, &res.physical_device_props);
        if (!test_device_properties(res.physical_device_props))
            res.is_suitable = false;
    }

    // required features
    {
        physical_device_feature_bundle feat_bundle;
        vkGetPhysicalDeviceFeatures2(device, feat_bundle.get());

        // always require GBV features right now (second arg)
        auto const has_required_features = set_or_test_device_features(feat_bundle.get(), true, true);
        if (!has_required_features)
            res.is_suitable = false;
    }

    // present modes and swapchain formats
    {
        uint32_t num_formats;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, nullptr);
        if (num_formats == 0)
            res.is_suitable = false;

        res.backbuffer_formats = cc::array<VkSurfaceFormatKHR>::uninitialized(num_formats);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, res.backbuffer_formats.data());

        uint32_t num_present_modes;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_present_modes, nullptr);
        if (num_present_modes == 0)
            res.is_suitable = false;

        res.present_modes = cc::array<VkPresentModeKHR>::uninitialized(num_present_modes);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_present_modes, res.present_modes.data());
    }

    // other queries
    {
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &res.surface_capabilities);
        vkGetPhysicalDeviceMemoryProperties(device, &res.mem_props);
    }

    return res;
}

cc::array<phi::vk::vulkan_gpu_info> phi::vk::get_all_vulkan_gpu_infos(VkInstance instance, VkSurfaceKHR surface)
{
    auto const physical_devices = get_physical_devices(instance);
    cc::array<vulkan_gpu_info> res(physical_devices.size());
    for (auto i = 0u; i < physical_devices.size(); ++i)
    {
        res[i] = get_vulkan_gpu_info(physical_devices[i], surface);
    }
    return res;
}

phi::vk::backbuffer_information phi::vk::get_backbuffer_information(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    backbuffer_information res;

    uint32_t num_formats;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, nullptr);
    CC_RUNTIME_ASSERT(num_formats != 0);
    res.backbuffer_formats = cc::array<VkSurfaceFormatKHR>::uninitialized(num_formats);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, res.backbuffer_formats.data());

    uint32_t num_present_modes;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_present_modes, nullptr);
    CC_RUNTIME_ASSERT(num_present_modes != 0);
    res.present_modes = cc::array<VkPresentModeKHR>::uninitialized(num_present_modes);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_present_modes, res.present_modes.data());

    return res;
}

VkSurfaceCapabilitiesKHR phi::vk::get_surface_capabilities(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VkSurfaceCapabilitiesKHR res;
    PHI_VK_VERIFY_NONERROR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &res));
    return res;
}

VkSurfaceFormatKHR phi::vk::choose_backbuffer_format(cc::span<const VkSurfaceFormatKHR> available_formats)
{
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
    case present_mode::allow_tearing:
        preferred = VK_PRESENT_MODE_IMMEDIATE_KHR;
        break;
    case present_mode::synced:
        preferred = VK_PRESENT_MODE_MAILBOX_KHR;
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

cc::vector<phi::gpu_info> phi::vk::get_available_gpus(cc::span<const vulkan_gpu_info> vk_gpu_infos)
{
    cc::vector<gpu_info> res;
    res.reserve(vk_gpu_infos.size());

    for (auto i = 0u; i < vk_gpu_infos.size(); ++i)
    {
        auto const& ll_info = vk_gpu_infos[i];

        auto& new_gpu = res.emplace_back();
        new_gpu.index = i;
        new_gpu.vendor = get_gpu_vendor_from_id(ll_info.physical_device_props.vendorID);
        new_gpu.description = cc::string(ll_info.physical_device_props.deviceName);

        // TODO: differentiate this somehow
        new_gpu.capabilities = ll_info.is_suitable ? gpu_capabilities::level_1 : gpu_capabilities::insufficient;
        new_gpu.has_raytracing = ll_info.available_layers_extensions.extensions.contains(VK_NV_RAY_TRACING_EXTENSION_NAME);
        new_gpu.dedicated_video_memory_bytes = 0;
        new_gpu.dedicated_system_memory_bytes = 0;
        new_gpu.shared_system_memory_bytes = 0;

        for (auto i = 0u; i < ll_info.mem_props.memoryHeapCount; ++i)
        {
            auto const& heap = ll_info.mem_props.memoryHeaps[i];

            if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT && ll_info.physical_device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                new_gpu.dedicated_video_memory_bytes += heap.size;
            else
                new_gpu.shared_system_memory_bytes += heap.size;
        }
    }

    return res;
}

bool phi::vk::set_or_test_device_features(VkPhysicalDeviceFeatures2* arg, bool enable_gbv, bool test_mode)
{
    // a single place to both test for existing features and set the features required


    // verify and unfold pNext chain
    CC_ASSERT(arg->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 && "sType for main argument wrong");
    CC_ASSERT(arg->pNext != nullptr && "pNext chain not long enough");

    VkPhysicalDeviceTimelineSemaphoreFeatures& p_next_chain_1 = *static_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(arg->pNext);
    CC_ASSERT(p_next_chain_1.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES && "pNext chain ordered unexpectedly");

    // clang-format off
#define PHI_LOC_ST(_prop_) \
    if (test_mode) { if (_prop_ != VK_TRUE) return false; } \
    else { _prop_ = VK_TRUE; } (void)0
    // clang-format on

#define PHI_LOC_ST_MAIN(_feat_) PHI_LOC_ST(arg->features._feat_)


    // == set and test features ==

    // added by discovered necessity
    PHI_LOC_ST_MAIN(samplerAnisotropy);
    PHI_LOC_ST_MAIN(geometryShader);

    // 100% support
    PHI_LOC_ST_MAIN(fillModeNonSolid);
    PHI_LOC_ST_MAIN(fragmentStoresAndAtomics);
    PHI_LOC_ST_MAIN(independentBlend);
    PHI_LOC_ST_MAIN(robustBufferAccess);

    // > 98% support
    PHI_LOC_ST_MAIN(drawIndirectFirstInstance);
    PHI_LOC_ST_MAIN(fullDrawIndexUint32);
    PHI_LOC_ST_MAIN(vertexPipelineStoresAndAtomics);
    PHI_LOC_ST_MAIN(imageCubeArray);
    PHI_LOC_ST_MAIN(multiDrawIndirect);
    PHI_LOC_ST_MAIN(shaderClipDistance);
    PHI_LOC_ST_MAIN(shaderCullDistance);
    PHI_LOC_ST_MAIN(dualSrcBlend);
    PHI_LOC_ST_MAIN(largePoints);
    PHI_LOC_ST_MAIN(logicOp);
    PHI_LOC_ST_MAIN(multiViewport);
    PHI_LOC_ST_MAIN(occlusionQueryPrecise);
    PHI_LOC_ST_MAIN(shaderSampledImageArrayDynamicIndexing);
    PHI_LOC_ST_MAIN(shaderStorageBufferArrayDynamicIndexing);
    PHI_LOC_ST_MAIN(shaderStorageImageArrayDynamicIndexing);
    PHI_LOC_ST_MAIN(shaderStorageImageWriteWithoutFormat);
    PHI_LOC_ST_MAIN(shaderTessellationAndGeometryPointSize);
    PHI_LOC_ST_MAIN(shaderUniformBufferArrayDynamicIndexing);
    PHI_LOC_ST_MAIN(textureCompressionBC);
    PHI_LOC_ST_MAIN(wideLines);
    PHI_LOC_ST_MAIN(depthBiasClamp);
    PHI_LOC_ST_MAIN(depthClamp);
    PHI_LOC_ST_MAIN(variableMultisampleRate);
    PHI_LOC_ST_MAIN(inheritedQueries);
    PHI_LOC_ST_MAIN(pipelineStatisticsQuery);
    PHI_LOC_ST_MAIN(sampleRateShading);
    PHI_LOC_ST_MAIN(shaderImageGatherExtended);
    PHI_LOC_ST_MAIN(shaderStorageImageExtendedFormats);
    PHI_LOC_ST_MAIN(tessellationShader);


    if (enable_gbv)
    {
        // features required for GBV
        PHI_LOC_ST_MAIN(fragmentStoresAndAtomics);
        PHI_LOC_ST_MAIN(vertexPipelineStoresAndAtomics);
    }

    PHI_LOC_ST(p_next_chain_1.timelineSemaphore);

    return true;

#undef PHI_LOC_ST_MAIN
#undef PHI_LOC_ST
}
