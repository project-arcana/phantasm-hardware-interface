#pragma once

#include <clean-core/array.hh>
#include <clean-core/vector.hh>

#include "loader/volk.hh"

namespace phi::vk
{
struct suitable_queues
{
    cc::vector<int> indices_graphics;
    cc::vector<int> indices_compute;
    cc::vector<int> indices_copy;
    cc::vector<int> indices_direct; ///< equivalent of D3D12 direct queue support (graphics + compute + copy)
};

struct chosen_queues
{
    int direct = -1;
    int separate_graphics = -1;
    int separate_compute = -1;
    int separate_copy = -1;
};

[[nodiscard]] suitable_queues get_suitable_queues(VkPhysicalDevice physical, VkSurfaceKHR surface);

[[nodiscard]] chosen_queues get_chosen_queues(suitable_queues const& suitable);

}
