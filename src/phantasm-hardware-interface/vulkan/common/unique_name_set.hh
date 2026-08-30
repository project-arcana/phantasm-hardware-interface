#pragma once

#include <cstring>

#include <clean-core/alloc_vector.hh>
#include <clean-core/span.hh>
#include <clean-core/stringhash.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk
{
// like strncpy but useful, https://linux.die.net/man/3/strlcpy
// TODO should move this somewhere common
inline size_t phi_strlcpy(char* __restrict dst, char const* __restrict src, size_t size)
{
    size_t srclen = std::strlen(src);
    if (size > 0)
    {
        size_t len = cc::min(srclen, size - 1);
        std::memcpy(dst, src, len);
        dst[len] = '\0';
    }
    return srclen;
}

struct FixedName
{
    // max length of vk ext names
    char str[256];
};


/// Helper to track unique names of layers and extensions
struct unique_name_set
{
public:
    void reset_reserve(cc::allocator* alloc, size_t num)
    {
        _names.reset_reserve(alloc, num);
        _name_hashes.reset_reserve(alloc, num);
    }

    void add(char const* value)
    {
        uint64_t const hash = cc::stringhash(value);
        if (contains(hash))
            return;

        _name_hashes.push_back(hash);
        FixedName& newName = _names.emplace_back();
        phi_strlcpy(newName.str, value, sizeof(newName.str));
    }

    void add(cc::span<VkExtensionProperties const> ext_props)
    {
        for (auto const& ext_prop : ext_props)
            add(ext_prop.extensionName);
    }

    bool contains(char const* value) const { return contains(cc::stringhash(value)); }

    bool contains(uint64_t hash) const
    {
        for (uint64_t v : _name_hashes)
        {
            if (hash == v)
                return true;
        }
        return false;
    }

private:
    cc::alloc_vector<FixedName> _names;
    cc::alloc_vector<uint64_t> _name_hashes;
};

} // namespace phi::vk
