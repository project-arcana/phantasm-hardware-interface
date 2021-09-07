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
    }
    CC_UNREACHABLE_SWITCH_WORKAROUND(pref);
}

char const* get_vendor_literal(phi::gpu_vendor vendor)
{
    switch (vendor)
    {
    case phi::gpu_vendor::amd:
        return "AMD";
    case phi::gpu_vendor::intel:
        return "Intel";
    case phi::gpu_vendor::nvidia:
        return "Nvidia";
    case phi::gpu_vendor::imgtec:
        return "ImgTec";
    case phi::gpu_vendor::arm:
        return "ARM";
    case phi::gpu_vendor::qualcomm:
        return "Qualcomm";
    case phi::gpu_vendor::unknown:
        return "Unknown";
    }
    CC_UNREACHABLE_SWITCH_WORKAROUND(vendor);
}
} // namespace

size_t phi::getPreferredGPU(cc::span<const phi::gpu_info> candidates, phi::adapter_preference preference)
{
    auto const F_GetFirstCapable = [&]() -> size_t
    {
        for (auto i = 0u; i < candidates.size(); ++i)
        {
            return i;
        }

        PHI_LOG_ERROR("Fatal: Found no suitable GPU (in {} candidate{})", candidates.size(), candidates.size() == 1 ? "" : "s");
        return candidates.size();
    };

    auto const F_MakeChoice = [&]() -> size_t
    {
        if (candidates.empty())
            return candidates.size();

        switch (preference)
        {
        case adapter_preference::integrated:
        {
            for (auto i = 0u; i < candidates.size(); ++i)
            {
                // Note that AMD also manufactures integrated GPUs, this is a heuristic
                if (candidates[i].vendor == gpu_vendor::intel)
                    return i;
            }

            // Fall back to the first adapter
            return F_GetFirstCapable();
        }
        case adapter_preference::highest_vram:
        {
            auto highest_vram_index = F_GetFirstCapable();

            for (auto i = 0u; i < candidates.size(); ++i)
            {
                if (candidates[i].dedicated_video_memory_bytes > candidates[highest_vram_index].dedicated_video_memory_bytes)
                    highest_vram_index = i;
            }

            return highest_vram_index;
        }
        case adapter_preference::first:
            return F_GetFirstCapable();
        case adapter_preference::explicit_index:
            return candidates.size();
        }

        return F_GetFirstCapable();
    };

    return F_MakeChoice();
}

phi::gpu_vendor phi::getGPUVendorFromPCIeID(unsigned vendor_id)
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

void phi::printStartupMessage(size_t numCandidates, gpu_info const* chosenCandidate, const phi::backend_config& config, bool is_d3d12)
{
    if (!config.print_startup_message)
        return;

    PHI_LOG("{} backend initialized, validation: {}", //
            is_d3d12 ? "D3D12" : "Vulkan", enum_to_string(config.validation));

    PHI_LOG("   {} threads, max {} resources, max {} PSOs", //
            config.num_threads, config.max_num_resources, config.max_num_pipeline_states);

    if (chosenCandidate != nullptr)
    {
        PHI_LOG("   {} ({}, index #{})", //
                chosenCandidate->name, get_vendor_literal(chosenCandidate->vendor), chosenCandidate->index);
    }
    else
    {
        PHI_LOG("   failed to choose gpu from {} candidate{}, preference: {}", //
                numCandidates, (numCandidates == 1 ? "" : "s"), get_preference_literal(config.adapter));
    }
}
