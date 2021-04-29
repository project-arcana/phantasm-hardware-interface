#pragma once

#include <clean-core/span.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/fwd.hh>

#include "common/unique_name_set.hh"
#include "loader/volk.hh"

namespace phi::vk
{
struct layer_extension_bundle
{
    VkLayerProperties layer_properties;
    cc::vector<VkExtensionProperties> extension_properties;

    layer_extension_bundle() { layer_properties = {}; }
    layer_extension_bundle(VkLayerProperties layer_props) : layer_properties(layer_props) {}
};

void write_instance_extensions(cc::vector<VkExtensionProperties>& out_extensions, const char* layername);
void write_device_extensions(cc::vector<VkExtensionProperties>& out_extensions, VkPhysicalDevice device, const char* layername);

struct layer_extension_set
{
    unique_name_set<vk_name_type::layer> layers;
    unique_name_set<vk_name_type::extension> extensions;
};

struct layer_extension_array
{
    // These are char const* intentionally for Vulkan interop
    // Only add literals!
    cc::vector<char const*> layers;
    cc::vector<char const*> extensions;
};

[[nodiscard]] layer_extension_set get_available_instance_lay_ext();
[[nodiscard]] layer_extension_set get_available_device_lay_ext(VkPhysicalDevice physical);

[[nodiscard]] layer_extension_array get_used_instance_lay_ext(layer_extension_set const& available, backend_config const& config);
[[nodiscard]] layer_extension_array get_used_device_lay_ext(layer_extension_set const& available, backend_config const& config, bool& has_raytracing, bool& has_conservative_raster);

}
