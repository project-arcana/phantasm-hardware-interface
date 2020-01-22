#pragma once

#include <string>
#include <vector>

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/fwd.hh>

#include "common/unique_name_set.hh"
#include "loader/volk.hh"

namespace phi::vk
{
struct layer_extension_bundle
{
    VkLayerProperties layer_properties;
    std::vector<VkExtensionProperties> extension_properties;

    layer_extension_bundle() { layer_properties = {}; }
    layer_extension_bundle(VkLayerProperties layer_props) : layer_properties(layer_props) {}

    void fill_with_instance_extensions(char const* layername = nullptr);
    void fill_with_device_extensions(VkPhysicalDevice device, char const* layername = nullptr);
};

// TODO: Name
struct lay_ext_set
{
    unique_name_set<vk_name_type::layer> layers;
    unique_name_set<vk_name_type::extension> extensions;
};

// TODO: Name
struct lay_ext_array
{
    // These are char const* intentionally for Vulkan interop
    // Only add literals!
    std::vector<char const*> layers;
    std::vector<char const*> extensions;
};

[[nodiscard]] lay_ext_set get_available_instance_lay_ext();
[[nodiscard]] lay_ext_set get_available_device_lay_ext(VkPhysicalDevice physical);

[[nodiscard]] lay_ext_array get_used_instance_lay_ext(lay_ext_set const& available, backend_config const& config);
[[nodiscard]] lay_ext_array get_used_device_lay_ext(lay_ext_set const& available, backend_config const& config, bool &has_raytracing);

}

