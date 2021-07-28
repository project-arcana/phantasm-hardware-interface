#pragma once

#include <clean-core/alloc_array.hh>
#include <clean-core/alloc_vector.hh>
#include <clean-core/span.hh>

#include <phantasm-hardware-interface/fwd.hh>

#include "common/unique_name_set.hh"
#include "loader/volk.hh"

namespace phi::vk
{
struct LayerExtensionBundle
{
    VkLayerProperties layerProperties = {};
    cc::alloc_array<VkExtensionProperties> extensionProperties;

    LayerExtensionBundle() = default;
    LayerExtensionBundle(VkLayerProperties const& prop) : layerProperties(prop) {}
};

cc::alloc_array<VkExtensionProperties> getInstanceExtensions(char const* layername, cc::allocator* alloc);
cc::alloc_array<VkExtensionProperties> getDeviceExtensions(VkPhysicalDevice device, char const* layername, cc::allocator* alloc);

struct LayerExtensionSet
{
    unique_name_set layers;
    unique_name_set extensions;
};

struct LayerExtensionArray
{
    // These are char const* intentionally for Vulkan interop
    // Only add literals!
    cc::alloc_vector<char const*> layers;
    cc::alloc_vector<char const*> extensions;
};

LayerExtensionSet getAvailableInstanceExtensions(cc::allocator* alloc);
LayerExtensionSet getAvailableDeviceExtensions(VkPhysicalDevice physical, cc::allocator* alloc);

LayerExtensionArray getUsedInstanceExtensions(LayerExtensionSet const& available, backend_config const& config);
LayerExtensionArray getUsedDeviceExtensions(LayerExtensionSet const& available, backend_config const& config, bool& outHasRaytracing, bool& outHasConservativeRaster);

} // namespace phi::vk
