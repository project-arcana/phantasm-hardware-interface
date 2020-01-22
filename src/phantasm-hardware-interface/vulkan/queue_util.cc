#include "queue_util.hh"

#include "common/verify.hh"

pr::backend::vk::suitable_queues pr::backend::vk::get_suitable_queues(VkPhysicalDevice physical, VkSurfaceKHR surface)
{
    uint32_t num_families;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &num_families, nullptr);
    cc::vector<VkQueueFamilyProperties> queue_families(num_families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &num_families, queue_families.data());

    suitable_queues res;
    for (auto i = 0u; i < num_families; ++i)
    {
        auto const& family = queue_families[i];

        bool is_graphics_capable = (family.queueFlags & VK_QUEUE_GRAPHICS_BIT);
        bool is_compute_capable = (family.queueFlags & VK_QUEUE_COMPUTE_BIT);
        bool is_copy_capable = (family.queueFlags & VK_QUEUE_TRANSFER_BIT);
        bool can_present = false;

        if (is_graphics_capable)
        {
            // graphics candidate, query present support
            VkBool32 present_support = false;
            PR_VK_VERIFY_SUCCESS(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present_support));

            if (present_support)
                can_present = true;
        }

        if (is_graphics_capable && can_present)
            res.indices_graphics.push_back(int(i));

        if (is_compute_capable)
            res.indices_compute.push_back(int(i));

        if (is_copy_capable)
            res.indices_copy.push_back(int(i));

        if (is_graphics_capable && is_compute_capable && is_copy_capable && can_present)
            res.indices_direct.push_back(int(i));
    }

    return res;
}

pr::backend::vk::chosen_queues pr::backend::vk::get_chosen_queues(const pr::backend::vk::suitable_queues& suitable)
{
    chosen_queues res;

    if (!suitable.indices_direct.empty())
        res.direct = suitable.indices_direct.front();

    // NOTE: technically all of the || == -1 checks are redundant here

    // search a graphics queue thats different from the direct one
    for (auto const gq : suitable.indices_graphics)
    {
        if (res.direct == -1 || gq != res.direct)
        {
            res.separate_graphics = gq;
            break;
        }
    }

    // search a compute queue thats different from direct and graphics
    for (auto const cq : suitable.indices_compute)
    {
        if ((res.direct == -1 || cq != res.direct) &&                    //
            (res.separate_graphics == -1 || cq != res.separate_graphics) //
        )
        {
            res.separate_compute = cq;
            break;
        }
    }

    // search a copy queue thats different from the three previous ones
    for (auto const cq : suitable.indices_copy)
    {
        if ((res.direct == -1 || cq != res.direct) &&                       //
            (res.separate_graphics == -1 || cq != res.separate_graphics) && //
            (res.separate_compute == -1 || cq != res.separate_compute)      //
        )
        {
            res.separate_copy = cq;
            break;
        }
    }

    return res;
}
