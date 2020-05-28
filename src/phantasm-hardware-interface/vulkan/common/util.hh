#pragma once

#include <cstdarg>
#include <cstdio>

#include <typed-geometry/types/size.hh>

#include <clean-core/always_false.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/span.hh>

#include <phantasm-hardware-interface/vulkan/loader/vulkan_fwd.hh>

#include <phantasm-hardware-interface/types.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk::util
{
[[nodiscard]] cc::capped_vector<VkVertexInputAttributeDescription, 16> get_native_vertex_format(cc::span<vertex_attribute_info const> attrib_info);


void set_object_name(VkDevice device, VkObjectType obj_type, void* obj_handle, const char* string);

namespace detail
{
template <class VkT>
inline constexpr VkObjectType get_object_type()
{
    if constexpr (std::is_same_v<VkT, VkBuffer_T>)
        return VK_OBJECT_TYPE_BUFFER;
    else if constexpr (std::is_same_v<VkT, VkImage_T>)
        return VK_OBJECT_TYPE_IMAGE;
    else if constexpr (std::is_same_v<VkT, VkShaderModule_T>)
        return VK_OBJECT_TYPE_SHADER_MODULE;
    else if constexpr (std::is_same_v<VkT, VkFence_T>)
        return VK_OBJECT_TYPE_FENCE;
    else if constexpr (std::is_same_v<VkT, VkAccelerationStructureNV_T> || std::is_same_v<VkT, VkAccelerationStructureKHR_T>)
        return VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV;
    else
    {
        static_assert(cc::always_false<VkT>, "unknown vulkan object type");
        return VK_OBJECT_TYPE_UNKNOWN;
    }
}

template <class VkT>
inline constexpr VkObjectType as_obj_type_enum = get_object_type<VkT>();
}

template <class VkT>
CC_PRINTF_FUNC(3, 4)
void set_object_name(VkDevice device, VkT* object, char const* fmt, ...)
{
    char name_formatted[1024];
    {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(name_formatted, 1024, fmt, args);
        va_end(args);
    }

    set_object_name(device, detail::as_obj_type_enum<VkT>, object, name_formatted);
}
}
