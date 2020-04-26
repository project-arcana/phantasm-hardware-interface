#include "queue_util.hh"

#include <clean-core/capped_array.hh>

#include "common/verify.hh"

phi::vk::suitable_queues phi::vk::get_suitable_queues(VkPhysicalDevice physical, VkSurfaceKHR surface)
{
    uint32_t num_families;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &num_families, nullptr);
    cc::vector<VkQueueFamilyProperties> queue_families(num_families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &num_families, queue_families.data());

    suitable_queues res;
    res.families.resize(num_families);

    for (auto i = 0u; i < num_families; ++i)
    {
        auto const& vk_family = queue_families[i];
        auto& family = res.families[i];

        family.num_queues = vk_family.queueCount;

        using capbit = suitable_queues::queue_capability;

        if (vk_family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            family.capabilities |= capbit::vk_graphics;

        if (vk_family.queueFlags & VK_QUEUE_COMPUTE_BIT)
            family.capabilities |= capbit::vk_compute;

        if (vk_family.queueFlags & VK_QUEUE_TRANSFER_BIT)
            family.capabilities |= capbit::vk_transfer;

        // query present support
        VkBool32 present_support = false;
        PHI_VK_VERIFY_SUCCESS(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present_support));

        if (present_support)
            family.capabilities |= capbit::present;

        if (family.capabilities & capbit::phi_direct)
            res.has_direct_queue = true;
    }

    return res;
}

phi::vk::chosen_queues phi::vk::get_chosen_queues(const phi::vk::suitable_queues& suitable)
{
    cc::capped_array<int, 16> queue_occupancy;
    queue_occupancy.emplace(suitable.families.size(), 0);

    auto const f_acquire_queue_index = [&](unsigned family_index) -> int {
        auto const& fam = suitable.families[family_index];
        int& occupancy = queue_occupancy[family_index];
        CC_ASSERT(occupancy < int(fam.num_queues) && "queue family overcommitted");
        return occupancy++;
    };

    auto const f_check_family = [&](unsigned family_index, uint32_t caps, uint32_t restriction) -> bool {
        auto const& fam = suitable.families[family_index];
        return fam.supports_exclusive(caps, restriction) && queue_occupancy[family_index] < int(fam.num_queues);
    };

    auto const f_check_direct_family = [&](unsigned family_index) -> bool {
        auto const& fam = suitable.families[family_index];
        return fam.supports(suitable_queues::phi_direct) && queue_occupancy[family_index] < int(fam.num_queues);
    };

    chosen_queues res;
    // three loops

    // search for a copy-only queue
    for (auto i = 0u; i < suitable.families.size(); ++i)
    {
        if (f_check_family(i, suitable_queues::phi_copy, suitable_queues::phi_compute))
        {
            res.copy.family_index = int(i);
            res.copy.queue_index = f_acquire_queue_index(i);
            break;
        }
    }

    // at this point, ONLY exclusive-copy queues are acquired,
    // acquires in the next loop do not have to check yet

    // search for a compute-only or compute-and-copy queue
    for (auto i = 0u; i < suitable.families.size(); ++i)
    {
        if (!res.compute.valid() && f_check_family(i, suitable_queues::phi_compute, suitable_queues::phi_direct))
        {
            res.compute.family_index = int(i);
            res.compute.queue_index = f_acquire_queue_index(i);
        }

        if (!res.copy.valid() && f_check_family(i, suitable_queues::phi_copy, suitable_queues::phi_direct))
        {
            res.copy.family_index = int(i);
            res.copy.queue_index = f_acquire_queue_index(i);
        }
    }

    // at this point, no direct queues are acquired and thus don't have to check

    // search for a direct queue, and fill in remaining queues
    for (auto i = 0u; i < suitable.families.size(); ++i)
    {
        if (!res.direct.valid() && f_check_direct_family(i))
        {
            res.direct.family_index = int(i);
            res.direct.queue_index = f_acquire_queue_index(i);
        }

        if (!res.compute.valid() && f_check_direct_family(i))
        {
            res.compute.family_index = int(i);
            res.compute.queue_index = f_acquire_queue_index(i);
        }

        if (!res.copy.valid() && f_check_direct_family(i))
        {
            res.copy.family_index = int(i);
            res.copy.queue_index = f_acquire_queue_index(i);
        }
    }

    return res;
}
