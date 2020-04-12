#include "layer_extension_util.hh"

#include <phantasm-hardware-interface/config.hh>

#include <clean-core/utility.hh>
#include <clean-core/vector.hh>

#include "common/log.hh"
#include "common/unique_name_set.hh"
#include "common/verify.hh"
#include "surface_util.hh"

void phi::vk::write_instance_extensions(cc::vector<VkExtensionProperties>& out_extensions, const char* layername)
{
    uint32_t res_count = cc::max<uint32_t>(10, uint32_t(out_extensions.capacity()));
    out_extensions.resize(res_count);

    VkResult res = vkEnumerateInstanceExtensionProperties(layername, &res_count, out_extensions.data());
    PHI_VK_ASSERT_NONERROR(res);

    if (res == VK_INCOMPLETE)
    {
        // more than the initial size was required
        // query the number required
        PHI_VK_VERIFY_NONERROR(vkEnumerateInstanceExtensionProperties(layername, &res_count, nullptr));

        // resize the array and re-query contents
        CC_ASSERT(res_count > 0);
        out_extensions.resize(res_count);
        PHI_VK_VERIFY_SUCCESS(vkEnumerateInstanceExtensionProperties(layername, &res_count, out_extensions.data()));
    }

    // shrink size to actually written elements (maintaining capacity)
    out_extensions.resize(res_count);
}

void phi::vk::write_device_extensions(cc::vector<VkExtensionProperties>& out_extensions, VkPhysicalDevice device, const char* layername)
{
    uint32_t res_count = cc::max<uint32_t>(10, uint32_t(out_extensions.capacity()));
    out_extensions.resize(res_count);

    VkResult res = vkEnumerateDeviceExtensionProperties(device, layername, &res_count, out_extensions.data());
    PHI_VK_ASSERT_NONERROR(res);

    if (res == VK_INCOMPLETE)
    {
        // more than the initial size was required
        // query the number required
        PHI_VK_VERIFY_NONERROR(vkEnumerateDeviceExtensionProperties(device, layername, &res_count, nullptr));

        // resize the array and re-query contents
        CC_ASSERT(res_count > 0);
        out_extensions.resize(res_count);
        PHI_VK_VERIFY_SUCCESS(vkEnumerateDeviceExtensionProperties(device, layername, &res_count, out_extensions.data()));
    }

    // shrink size to actually written elements (maintaining capacity)
    out_extensions.resize(res_count);
}

phi::vk::lay_ext_set phi::vk::get_available_instance_lay_ext()
{
    lay_ext_set available_res;

    // Add global instance layer's extensions
    {
        cc::vector<VkExtensionProperties> global_extensions;
        write_instance_extensions(global_extensions, nullptr);
        available_res.extensions.add(global_extensions);
    }

    // Enumerate instance layers
    {
        VkResult res;
        cc::vector<VkLayerProperties> global_layer_properties;

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
            layer_extension_bundle layer;
            layer.layer_properties = layer_prop;
            write_instance_extensions(layer.extension_properties, layer_prop.layerName);
            available_res.extensions.add(layer.extension_properties);
            available_res.layers.add(layer_prop.layerName);
        }
    }

    return available_res;
}


phi::vk::lay_ext_set phi::vk::get_available_device_lay_ext(VkPhysicalDevice physical)
{
    lay_ext_set available_res;

    cc::vector<layer_extension_bundle> layer_extensions;

    // Add global device layer
    layer_extensions.emplace_back();

    // Enumerate device layers
    {
        uint32_t count = 0;

        PHI_VK_VERIFY_SUCCESS(vkEnumerateDeviceLayerProperties(physical, &count, nullptr));

        cc::vector<VkLayerProperties> layer_properties;
        layer_properties.resize(count);

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
            write_device_extensions(layer.extension_properties, physical, nullptr);
        }
        else
        {
            available_res.layers.add(layer.layer_properties.layerName);
            write_device_extensions(layer.extension_properties, physical, layer.layer_properties.layerName);
        }

        available_res.extensions.add(layer.extension_properties);
    }

    return available_res;
}


phi::vk::lay_ext_array phi::vk::get_used_instance_lay_ext(const phi::vk::lay_ext_set& available, const phi::backend_config& config)
{
    lay_ext_array used_res;

    auto const add_layer = [&](char const* layer_name) {
        if (available.layers.contains(layer_name))
        {
            used_res.layers.push_back(layer_name);
            return true;
        }
        return false;
    };

    auto const add_ext = [&](char const* ext_name) {
        if (available.extensions.contains(ext_name))
        {
            used_res.extensions.push_back(ext_name);
            return true;
        }
        return false;
    };

    // Decide upon active instance layers and extensions based on configuration and availability
    if (config.validation >= validation_level::on)
    {
        if (!add_layer("VK_LAYER_KHRONOS_validation"))
        {
            log::err() << "Validation enabled, but no layers available on Vulkan instance";
            log::err() << "Download the LunarG SDK for your operating system,";
            log::err() << "then set these environment variables: (all paths absolute)";
            log::err() << "VK_LAYER_PATH - <sdk>/x86_64/etc/vulkan/explicit_layer.d/";
            log::err() << "VULKAN_SDK - <sdk>/x86_64/bin";
            log::err() << "LD_LIBRARY_PATH - <VALUE>:<sdk>/x86_64/lib (append)";
        }

        if (!add_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        {
            log::err() << "Missing debug utils extension";
        }
    }

    if (config.validation >= validation_level::on_extended)
    {
        if (!add_ext("VK_EXT_validation_features"))
        {
            log::err() << "Missing GPU-assisted validation extension";
        }
    }

    if (config.native_features & backend_config::native_feature_vk_api_dump)
    {
        if (!add_layer("VK_LAYER_LUNARG_api_dump"))
        {
            log::err() << "Missing API dump layer";
        }
    }

    // platform extensions
    for (char const* const required_device_ext : get_platform_instance_extensions())
    {
        if (!add_ext(required_device_ext))
            log::err() << "Missing required extension" << required_device_ext;
    }


    return used_res;
}

phi::vk::lay_ext_array phi::vk::get_used_device_lay_ext(const phi::vk::lay_ext_set& available, const phi::backend_config& config, bool& has_raytracing)
{
    lay_ext_array used_res;

    [[maybe_unused]] auto const add_layer = [&](char const* layer_name) {
        if (available.layers.contains(layer_name))
        {
            used_res.layers.push_back(layer_name);
            return true;
        }
        return false;
    };

    auto const add_ext = [&](char const* ext_name) {
        if (available.extensions.contains(ext_name))
        {
            used_res.extensions.push_back(ext_name);
            return true;
        }
        return false;
    };

    if (!add_ext(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
    {
        log::err() << "Missing swapchain extension";
    }

    has_raytracing = false;

    if (config.enable_raytracing)
    {
        if (add_ext(VK_NV_RAY_TRACING_EXTENSION_NAME))
        {
            if (add_ext(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME))
            {
                has_raytracing = true;
            }
            else
            {
                log::err()("Missing raytracing extension dependency {}", VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
            }
        }
    }

    return used_res;
}
