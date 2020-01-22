#include "util.hh"

#include <clean-core/assert.hh>
#include <clean-core/bit_cast.hh>

#include "vk_format.hh"

cc::capped_vector<VkVertexInputAttributeDescription, 16> pr::backend::vk::util::get_native_vertex_format(cc::span<const pr::backend::vertex_attribute_info> attrib_info)
{
    cc::capped_vector<VkVertexInputAttributeDescription, 16> res;
    for (auto const& ai : attrib_info)
    {
        VkVertexInputAttributeDescription& elem = res.emplace_back();
        elem.binding = 0;
        elem.location = uint32_t(res.size() - 1);
        elem.format = util::to_vk_format(ai.format);
        elem.offset = ai.offset;
    }

    return res;
}

VkVertexInputBindingDescription pr::backend::vk::util::get_vertex_binding(uint32_t vertex_size)
{
    VkVertexInputBindingDescription res = {};
    res.binding = 0;
    res.stride = vertex_size;
    res.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return res;
}

void pr::backend::vk::util::set_object_name(VkDevice device, VkObjectType obj_type, void* obj_handle, const char* string)
{
    if (vkSetDebugUtilsObjectNameEXT)
    {
        VkDebugUtilsObjectNameInfoEXT name_info = {};
        name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        name_info.objectType = obj_type;
        name_info.objectHandle = cc::bit_cast<uint64_t>(obj_handle);
        name_info.pObjectName = string;
        vkSetDebugUtilsObjectNameEXT(device, &name_info);
    }
}
