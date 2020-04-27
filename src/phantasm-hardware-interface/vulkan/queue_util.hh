#pragma once

#include <clean-core/array.hh>
#include <clean-core/vector.hh>

#include "loader/volk.hh"

namespace phi::vk
{
struct suitable_queues
{
    enum queue_capability : uint32_t
    {
        none = 0,

        present = 1 << 0,
        vk_graphics = 1 << 1,
        vk_compute = 1 << 2,
        vk_transfer = 1 << 3,

        phi_graphics = vk_graphics | present,
        phi_compute = vk_compute | present, // we allow present_from_compute globally
        phi_copy = vk_transfer,
        phi_direct = phi_graphics | phi_compute | phi_copy
    };

    struct queue_family
    {
        uint32_t capabilities = queue_capability::none;
        uint32_t num_queues = 0;

        bool supports(uint32_t caps) const { return capabilities & caps; }
        bool supports_exclusive(uint32_t caps, uint32_t restriction) const { return (capabilities & caps) && !(capabilities & restriction); }
    };

    // indexed 1:1 as the queues queried from Vk
    cc::vector<queue_family> families;
    bool has_direct_queue = false;
};

struct chosen_queues
{
    struct queue_indices
    {
        int family_index = -1;
        int queue_index = 0;

    public:
        bool valid() const { return family_index != -1; }
        bool operator==(queue_indices const& rhs) const { return family_index == rhs.family_index && queue_index == rhs.queue_index; }
        bool operator!=(queue_indices const& rhs) const { return !this->operator==(rhs); }
    };

    queue_indices direct;
    queue_indices compute;
    queue_indices copy;
};

[[nodiscard]] suitable_queues get_suitable_queues(VkPhysicalDevice physical, VkSurfaceKHR surface);

[[nodiscard]] chosen_queues get_chosen_queues(suitable_queues const& suitable);

}
