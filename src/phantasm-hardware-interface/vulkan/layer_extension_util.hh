#pragma once

#include <clean-core/span.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/fwd.hh>

#include "common/unique_name_set.hh"
#include "loader/volk.hh"

namespace phi::vk
{
struct LayerExtensionBundle
{
    VkLayerProperties layerProperties;
    cc::vector<VkExtensionProperties> extensionProperties;

    LayerExtensionBundle() { layerProperties = {}; }
    LayerExtensionBundle(VkLayerProperties layer_props) : layerProperties(layer_props) {}
};

void writeInstanceExtensions(cc::vector<VkExtensionProperties>& out_extensions, const char* layername);
void writeDeviceExtensions(cc::vector<VkExtensionProperties>& out_extensions, VkPhysicalDevice device, const char* layername);

struct LayerExtensionSet
{
    unique_name_set<vk_name_type::layer> layers;
    unique_name_set<vk_name_type::extension> extensions;
};

struct LayerExtensionArray
{
    // These are char const* intentionally for Vulkan interop
    // Only add literals!
    cc::vector<char const*> layers;
    cc::vector<char const*> extensions;
};

LayerExtensionSet getAvailableInstanceExtensions();
LayerExtensionSet getAvailableDeviceExtensions(VkPhysicalDevice physical);

LayerExtensionArray getUsedInstanceExtensions(LayerExtensionSet const& available, backend_config const& config);
LayerExtensionArray getUsedDeviceExtensions(LayerExtensionSet const& available, backend_config const& config, bool& outHasRaytracing, bool& outHasConservativeRaster);

}
