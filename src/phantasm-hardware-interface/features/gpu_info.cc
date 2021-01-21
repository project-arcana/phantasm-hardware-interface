#include "gpu_info.hh"

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/config.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/common/enums_from_string.hh>
#include <phantasm-hardware-interface/common/log.hh>

namespace
{
constexpr char const* get_preference_literal(phi::adapter_preference pref)
{
    switch (pref)
    {
    case phi::adapter_preference::first:
        return "first";
    case phi::adapter_preference::integrated:
        return "integrated";
    case phi::adapter_preference::highest_vram:
        return "highest vram";
    case phi::adapter_preference::explicit_index:
        return "explicit index";
    case phi::adapter_preference::highest_feature_level:
        return "highest feature level";
    }
    CC_UNREACHABLE_SWITCH_WORKAROUND(pref);
}
}

size_t phi::get_preferred_gpu(cc::span<const phi::gpu_info> candidates, phi::adapter_preference preference)
{
    auto const get_first_capable = [&]() -> size_t {
        for (auto i = 0u; i < candidates.size(); ++i)
        {
            if (candidates[i].capabilities != gpu_capabilities::insufficient)
                return i;
        }

        PHI_LOG_ERROR("Fatal: Found no suitable GPU (in {} candidate{})", candidates.size(), candidates.size() == 1 ? "" : "s");
        return candidates.size();
    };

    auto const make_choice = [&]() -> size_t {
        if (candidates.empty())
            return candidates.size();

        switch (preference)
        {
        case adapter_preference::integrated:
        {
            for (auto i = 0u; i < candidates.size(); ++i)
            {
                // Note that AMD also manufactures integrated GPUs, this is a heuristic
                if (candidates[i].capabilities != gpu_capabilities::insufficient && candidates[i].vendor == gpu_vendor::intel)
                    return i;
            }

            // Fall back to the first adapter
            return get_first_capable();
        }
        case adapter_preference::highest_vram:
        {
            auto highest_vram_index = get_first_capable();

            for (auto i = 0u; i < candidates.size(); ++i)
            {
                if (candidates[i].capabilities != gpu_capabilities::insufficient
                    && candidates[i].dedicated_video_memory_bytes > candidates[highest_vram_index].dedicated_video_memory_bytes)
                    highest_vram_index = i;
            }

            return highest_vram_index;
        }
        case adapter_preference::highest_feature_level:
        {
            auto highest_capability_index = get_first_capable();

            for (auto i = 1u; i < candidates.size(); ++i)
            {
                if (candidates[i].capabilities > candidates[highest_capability_index].capabilities)
                    highest_capability_index = i;
            }

            return highest_capability_index;
        }
        case adapter_preference::first:
            return get_first_capable();
        case adapter_preference::explicit_index:
            return candidates.size();
        }

        return get_first_capable();
    };

    return make_choice();
}

phi::gpu_vendor phi::get_gpu_info_from_pcie_id(unsigned vendor_id)
{
    switch (vendor_id)
    {
    case 0x1002:
        return gpu_vendor::amd;
    case 0x8086:
        return gpu_vendor::intel;
    case 0x10DE:
        return gpu_vendor::nvidia;
    case 0x1010:
        return gpu_vendor::imgtec;
    case 0x13B5:
        return gpu_vendor::arm;
    case 0x5143:
        return gpu_vendor::qualcomm;
    default:
        return gpu_vendor::unknown;
    }
}

void phi::print_startup_message(cc::span<const phi::gpu_info> gpu_candidates, size_t chosen_index, const phi::backend_config& config, bool is_d3d12)
{
    if (!config.print_startup_message)
        return;

    PHI_LOG("{} backend initialized, validation: {}", //
            is_d3d12 ? "d3d12" : "vulkan", enum_to_string(config.validation));

    PHI_LOG("   {} threads, max {} resources, max {} PSOs", //
            config.num_threads, config.max_num_resources, config.max_num_pipeline_states);

    if (chosen_index < gpu_candidates.size())
    {
        PHI_LOG("   chose gpu #{} ({}) from {} candidate{}, preference: {}", //
                chosen_index, gpu_candidates[chosen_index].name, gpu_candidates.size(), (gpu_candidates.size() == 1 ? "" : "s"),
                get_preference_literal(config.adapter));
    }
    else
    {
        PHI_LOG("   failed to choose gpu from {} candidate{}, preference: {}", //
                gpu_candidates.size(), (gpu_candidates.size() == 1 ? "" : "s"), get_preference_literal(config.adapter));
    }
}
