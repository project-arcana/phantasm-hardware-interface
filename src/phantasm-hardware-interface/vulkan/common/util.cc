#include "util.hh"

#include <clean-core/assert.hh>
#include <clean-core/bit_cast.hh>

#include "vk_format.hh"

cc::capped_vector<VkVertexInputAttributeDescription, 16> phi::vk::util::get_native_vertex_format(cc::span<const phi::vertex_attribute_info> attrib_info)
{
    cc::capped_vector<VkVertexInputAttributeDescription, 16> res;
    for (auto const& ai : attrib_info)
    {
        VkVertexInputAttributeDescription& elem = res.emplace_back();
        elem.binding = ai.vertex_buffer_i;
        elem.location = uint32_t(res.size() - 1);
        elem.format = util::to_vk_format(ai.fmt);
        elem.offset = ai.offset;
    }

    return res;
}

void phi::vk::util::set_object_name(VkDevice device, VkObjectType obj_type, void* obj_handle, const char* string)
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
