#include "gpu_choice_util.hh"

#include <cstdint>

#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/limits.hh>

#include "common/verify.hh"
#include "queue_util.hh"

namespace
{
[[nodiscard]] bool are_device_properties_sufficient(VkPhysicalDeviceProperties const& props)
{
    if (props.limits.maxBoundDescriptorSets < pr::backend::limits::max_shader_arguments * 2)
        return false;

    if (props.limits.maxColorAttachments < pr::backend::limits::max_render_targets)
        return false;

    return true;
}
}

cc::array<VkPhysicalDevice> pr::backend::vk::get_physical_devices(VkInstance instance)
{
    uint32_t num_physical_devices;
    PR_VK_VERIFY_NONERROR(vkEnumeratePhysicalDevices(instance, &num_physical_devices, nullptr));
    cc::array<VkPhysicalDevice> res(num_physical_devices);
    PR_VK_VERIFY_NONERROR(vkEnumeratePhysicalDevices(instance, &num_physical_devices, res.data()));
    return res;
}

pr::backend::vk::vulkan_gpu_info pr::backend::vk::get_vulkan_gpu_info(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    vulkan_gpu_info res;
    res.physical_device = device;
    res.is_suitable = true;

    // queue capability
    res.queues = get_suitable_queues(device, surface);
    if (res.queues.indices_graphics.empty())
        res.is_suitable = false;

    res.available_layers_extensions = get_available_device_lay_ext(device);

    // swapchain extensions
    if (!res.available_layers_extensions.extensions.contains(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        res.is_suitable = false;


    // device properties
    {
        vkGetPhysicalDeviceProperties(device, &res.physical_device_props);
        if (!are_device_properties_sufficient(res.physical_device_props))
            res.is_suitable = false;
    }

    // required features
    {
        VkPhysicalDeviceFeatures supported_features;
        vkGetPhysicalDeviceFeatures(device, &supported_features);

        if (!supported_features.samplerAnisotropy || !supported_features.geometryShader)
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

cc::array<pr::backend::vk::vulkan_gpu_info> pr::backend::vk::get_all_vulkan_gpu_infos(VkInstance instance, VkSurfaceKHR surface)
{
    auto const physical_devices = get_physical_devices(instance);
    cc::array<vulkan_gpu_info> res(physical_devices.size());
    for (auto i = 0u; i < physical_devices.size(); ++i)
    {
        res[i] = get_vulkan_gpu_info(physical_devices[i], surface);
    }
    return res;
}

pr::backend::vk::backbuffer_information pr::backend::vk::get_backbuffer_information(VkPhysicalDevice device, VkSurfaceKHR surface)
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

VkSurfaceCapabilitiesKHR pr::backend::vk::get_surface_capabilities(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VkSurfaceCapabilitiesKHR res;
    PR_VK_VERIFY_NONERROR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &res));
    return res;
}

VkSurfaceFormatKHR pr::backend::vk::choose_backbuffer_format(cc::span<const VkSurfaceFormatKHR> available_formats)
{
    for (auto const& f : available_formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;

    return available_formats[0];
}

VkPresentModeKHR pr::backend::vk::choose_present_mode(cc::span<const VkPresentModeKHR> available_modes, present_mode mode)
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

VkExtent2D pr::backend::vk::get_swap_extent(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D extent_hint)
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

cc::vector<pr::backend::gpu_info> pr::backend::vk::get_available_gpus(cc::span<const vulkan_gpu_info> vk_gpu_infos)
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
