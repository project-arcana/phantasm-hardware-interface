#pragma once

#include <string>
#include <unordered_set>

#include <clean-core/span.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk
{
enum class vk_name_type
{
    layer,
    extension
};

/// Helper to track unique names of layers and extensions
template <vk_name_type T>
struct unique_name_set
{
public:
    void add(std::string const& value) { _names.insert(value); }
    void add(cc::span<std::string const> values)
    {
        for (auto const& v : values)
            _names.insert(v);
    }

    void add(cc::span<VkExtensionProperties const> ext_props)
    {
        static_assert(T == vk_name_type::extension, "Incompatible container");

        for (auto const& ext_prop : ext_props)
            _names.insert(ext_prop.extensionName);
    }

    [[nodiscard]] bool contains(std::string const& name) const { return _names.find(name) != _names.end(); }

private:
    // TODO: Implement this with a future cc hashmap and vector / iteration helper
    std::unordered_set<std::string> _names;
};

}
