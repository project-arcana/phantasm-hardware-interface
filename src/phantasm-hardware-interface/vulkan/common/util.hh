#pragma once

#include <cstdarg>
#include <cstdio>

#include <typed-geometry/types/size.hh>

#include <clean-core/always_false.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/span.hh>

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
    else if constexpr (std::is_same_v<VkT, VkSemaphore_T>)
        return VK_OBJECT_TYPE_SEMAPHORE;
    // NOTE: there is some chaos surrounding this struct in the KHR/NV transition, this works however
    else if constexpr (std::is_same_v<VkT, std::remove_pointer_t<VkAccelerationStructureNV>>)
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
void set_object_name(VkDevice device, VkT* object, char const* fmt, ...) CC_PRINTF_FUNC(3)
{
    char buf[1024];

    {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
    }

    set_object_name(device, detail::as_obj_type_enum<VkT>, object, buf);
}
}
