#include "layer_extension_util.hh"

#include <cstring>

#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/config.hh>

#include "common/unique_name_set.hh"
#include "common/verify.hh"
#include "surface_util.hh"

cc::alloc_array<VkExtensionProperties> phi::vk::getInstanceExtensions(char const* layername, cc::allocator* alloc)
{
    uint32_t num_ext;
    PHI_VK_VERIFY_NONERROR(vkEnumerateInstanceExtensionProperties(layername, &num_ext, nullptr));
    cc::alloc_array<VkExtensionProperties> res(num_ext, alloc);
    PHI_VK_VERIFY_NONERROR(vkEnumerateInstanceExtensionProperties(layername, &num_ext, res.data()));
    return res;
}

cc::alloc_array<VkExtensionProperties> phi::vk::getDeviceExtensions(VkPhysicalDevice device, char const* layername, cc::allocator* alloc)
{
    uint32_t num_ext;
    PHI_VK_VERIFY_NONERROR(vkEnumerateDeviceExtensionProperties(device, layername, &num_ext, nullptr));
    cc::alloc_array<VkExtensionProperties> res(num_ext, alloc);
    PHI_VK_VERIFY_NONERROR(vkEnumerateDeviceExtensionProperties(device, layername, &num_ext, res.data()));
    return res;
}

phi::vk::LayerExtensionSet phi::vk::getAvailableInstanceExtensions(cc::allocator* alloc)
{
    LayerExtensionSet available_res;
    available_res.layers.reset_reserve(alloc, 32);
    available_res.extensions.reset_reserve(alloc, 128);

    // Add global instance layer's extensions
    {
        auto global_extensions = getInstanceExtensions(nullptr, alloc);
        available_res.extensions.add(global_extensions);
    }

    // Enumerate instance layers
    {
        VkResult res;
        cc::alloc_vector<VkLayerProperties> global_layer_properties;
        global_layer_properties.reset_reserve(alloc, 32);

        do
        {
            uint32_t res_count;
            res = vkEnumerateInstanceLayerProperties(&res_count, nullptr);
            PHI_VK_ASSERT_NONERROR(res);

            if (res_count > 0)
            {
                global_layer_properties.resize(global_layer_properties.size() + res_count);
                // Append to global_layers
                res = vkEnumerateInstanceLayerProperties(&res_count, &global_layer_properties[global_layer_properties.size() - res_count]);
                PHI_VK_ASSERT_NONERROR(res);
            }

        } while (res == VK_INCOMPLETE);

        for (auto const& layer_prop : global_layer_properties)
        {
            LayerExtensionBundle layer;
            layer.layerProperties = layer_prop;
            layer.extensionProperties = getInstanceExtensions(layer_prop.layerName, alloc);

            available_res.extensions.add(layer.extensionProperties);
            available_res.layers.add(layer_prop.layerName);
        }
    }

    return available_res;
}


phi::vk::LayerExtensionSet phi::vk::getAvailableDeviceExtensions(VkPhysicalDevice physical, cc::allocator* alloc)
{
    LayerExtensionSet available_res;

    cc::alloc_vector<LayerExtensionBundle> layer_extensions;
    layer_extensions.reset_reserve(alloc, 16);

    // Add global device layer
    layer_extensions.emplace_back();

    // Enumerate device layers
    {
        uint32_t count = 0;

        PHI_VK_VERIFY_SUCCESS(vkEnumerateDeviceLayerProperties(physical, &count, nullptr));

        cc::alloc_array<VkLayerProperties> layer_properties;
        layer_properties.reset(alloc, count);

        PHI_VK_VERIFY_SUCCESS(vkEnumerateDeviceLayerProperties(physical, &count, layer_properties.data()));

        layer_extensions.reserve(layer_extensions.size() + layer_properties.size());
        for (auto const& layer_prop : layer_properties)
            layer_extensions.emplace_back(layer_prop);
    }

    // Track information for all additional device layers beyond the first
    for (auto i = 0u; i < layer_extensions.size(); ++i)
    {
        auto& layer = layer_extensions[i];
        if (i == 0)
        {
            layer.extensionProperties = getDeviceExtensions(physical, nullptr, alloc);
        }
        else
        {
            available_res.layers.add(layer.layerProperties.layerName);
            layer.extensionProperties = getDeviceExtensions(physical, layer.layerProperties.layerName, alloc);
        }

        available_res.extensions.add(layer.extensionProperties);
    }

    return available_res;
}


phi::vk::LayerExtensionArray phi::vk::getUsedInstanceExtensions(const phi::vk::LayerExtensionSet& available, const phi::backend_config& config)
{
    // All layers and extensions enabled in this function are instance-specific
    // Missing ones are not related to hardware, but most likely to the installed Vulkan SDK

    LayerExtensionArray used_res;

    auto const F_AddLayer = [&](char const* layer_name) -> bool {
        if (available.layers.contains(layer_name))
        {
            used_res.layers.push_back(layer_name);
            return true;
        }
        return false;
    };

    auto const F_AddExtension = [&](char const* ext_name) -> bool {
        if (available.extensions.contains(ext_name))
        {
            used_res.extensions.push_back(ext_name);
            return true;
        }
        return false;
    };

    bool hasShownSDKHelp = false;
    auto F_ShowSDKHelp = [&]() {
        if (hasShownSDKHelp)
            return;

        hasShownSDKHelp = true;
        PHI_LOG_WARN("  try downloaing the latest LunarG SDK for your operating system,");
        PHI_LOG_WARN("  then set these environment variables: (all paths absolute)");
        PHI_LOG_WARN("  VK_LAYER_PATH - <sdk>/x86_64/etc/vulkan/explicit_layer.d/");
        PHI_LOG_WARN("  VULKAN_SDK - <sdk>/x86_64/bin");
        PHI_LOG_WARN("  LD_LIBRARY_PATH - <VALUE>:<sdk>/x86_64/lib (append)");
    };

    // Decide upon active instance layers and extensions based on configuration and availability
    if (config.validation >= validation_level::on)
    {
        if (!F_AddLayer("VK_LAYER_KHRONOS_validation"))
        {
            PHI_LOG_ERROR(R"(Validation is enabled (validation_level::on or higher), but "VK_LAYER_KHRONOS_validation" is missing on this Vulkan instance)");
            F_ShowSDKHelp();
        }
    }

    if (config.validation >= validation_level::on_extended)
    {
        if (!F_AddExtension("VK_EXT_validation_features"))
        {
            PHI_LOG_ERROR(R"(GPU based validation is enabled (validation_level::on_extended or higher), but "VK_EXT_validation_features" is missing on this Vulkan instance)");
            F_ShowSDKHelp();
        }
    }

    if (config.native_features & backend_config::native_feature_vk_api_dump)
    {
        if (!F_AddLayer("VK_LAYER_LUNARG_api_dump"))
        {
            PHI_LOG_ERROR(R"(Vulkan API dump is enabled (native_feature_vk_api_dump), but "VK_LAYER_LUNARG_api_dump" is missing on this Vulkan instance)");
            F_ShowSDKHelp();
        }
        else
        {
            PHI_LOG("Vulkan API dump enabled - all calls are printed to stdout (native_feature_vk_api_dump)");
        }
    }

    // VK_EXT_debug_utils
    // for cmd::debug_marker, object debug names
    // spec: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_debug_utils.html
    // this is the revised version of VK_EXT_DEBUG_MARKER_EXTENSION_NAME (which is more or less deprecated)
    if (!F_AddExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        PHI_LOG_ERROR(R"(Missing debug utility extension "{}")", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        F_ShowSDKHelp();
    }

    // platform extensions
    for (char const* const required_device_ext : get_platform_instance_extensions())
    {
        if (!F_AddExtension(required_device_ext))
        {
            PHI_LOG_ERROR(R"(Missing platform-specific required Vulkan extension "{}")", required_device_ext);
            F_ShowSDKHelp();
        }
    }


    return used_res;
}

phi::vk::LayerExtensionArray phi::vk::getUsedDeviceExtensions(const phi::vk::LayerExtensionSet& available,
                                                              const phi::backend_config& config,
                                                              bool& outHasRaytracing,
                                                              bool& outHasConservativeRaster)
{
    LayerExtensionArray used_res;

    [[maybe_unused]] auto const f_add_layer = [&](char const* layer_name) {
        if (available.layers.contains(layer_name))
        {
            used_res.layers.push_back(layer_name);
            return true;
        }
        return false;
    };

    auto const f_add_ext = [&](char const* ext_name) {
        if (available.extensions.contains(ext_name))
        {
            used_res.extensions.push_back(ext_name);
            return true;
        }
        return false;
    };

    if (!f_add_ext(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
    {
        PHI_LOG_ERROR(R"(Fatal: Missing vulkan swapchain extension "{}")", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    // VK_KHR_timeline_semaphore  - core in Vk 1.2
    // if (!f_add_ext(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
    //{
    //    PHI_LOG_ERROR(R"(Missing vulkan timeline semaphore extension "{}", try updating GPU drivers)", VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    //}

    // VK_KHR_relaxed_block_layout - core in Vk 1.1
    // https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_relaxed_block_layout.html
    //// prevents debug layers from warning about non-std430 layouts,
    //// which occur ie. with the -fvk-use-dx-layout DXC flag
    // if (!f_add_ext(VK_KHR_RELAXED_BLOCK_LAYOUT_EXTENSION_NAME))
    //{
    //    PHI_LOG_WARN("missing relaxed block layout extension");
    //}

    // additional extensions
    outHasConservativeRaster = false;
    if (f_add_ext(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME))
    {
        outHasConservativeRaster = true;
    }

    outHasRaytracing = false;
    if (config.enable_raytracing)
    {
        // Note on Vk Ray tracing extensions:
        // as of 08.09.2020, VK_KHR_ray_tracing is still only available in beta Nvidia drivers
        // thus we use NV. API differences are not major
        if (f_add_ext("VK_NV_ray_tracing"))
        {
            bool all_dependencies_present = true;
            auto f_add_rt_dependency = [&](char const* name) {
                if (!f_add_ext(name))
                {
                    all_dependencies_present = false;
                    PHI_LOG_ERROR(R"(missing raytracing extension dependency "{}", try updating GPU drivers)", name);
                }
            };

            // VK_KHR_get_physical_device_properties2 - core in Vk 1.1
            // https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_get_physical_device_properties2.html
            // f_add_rt_dependency(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

            // VK_KHR_get_memory_requirements2 - core in Vk 1.1
            // https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_get_memory_requirements2.html
            // f_add_rt_dependency(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);

            // extensions below are required for KHR, but not NV
            // f_add_rt_dependency(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
            // f_add_rt_dependency(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            // f_add_rt_dependency("VK_KHR_deferred_host_operations");
            // f_add_rt_dependency("VK_KHR_pipeline_library");

            if (all_dependencies_present)
                outHasRaytracing = true;
        }
    }

    return used_res;
}
