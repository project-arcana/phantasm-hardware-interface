#include "Adapter.hh"

#include <clean-core/assert.hh>

#ifdef PHI_HAS_OPTICK
#include <optick/optick.h>
#endif

#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/config.hh>
#include <phantasm-hardware-interface/features/gpu_info.hh>

#include "adapter_choice_util.hh"
#include "common/d3d12_sanitized.hh"
#include "common/shared_com_ptr.hh"
#include "common/verify.hh"

bool phi::d3d12::Adapter::initialize(const backend_config& config, ID3D12Device*& outCreatedDevice)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT();
#endif

    // Suppress GBV startup message
    // TODO: This has no effect due to a bug
    // Jesse Natalie:
    // "[...] Apparently, the "most up-to-date" filter among all the ones that could possibly match are chosen.
    // The D3D12 device produces a filter during creation, which makes it more up-to-date than any that'd be
    // created before device creation is begun. [...]"
    // will revisit once that is fixed
    // also note, maybe we don't want this after all, makes for a good way to
    // verify that d3d12 output is appearing where you expect it to (message in Device::initialize)
#if 0
    if (config.validation != validation_level::off)
    {
        bool const dxgi_queue_success = detail::hr_succeeded(::DXGIGetDebugInterface1(0, PHI_COM_WRITE(mInfoQueue)));
        if (dxgi_queue_success && mInfoQueue.is_valid())
        {
            DXGI_INFO_QUEUE_FILTER filter = {};

            DXGI_INFO_QUEUE_MESSAGE_SEVERITY denied_severities[] = {DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE};
            filter.DenyList.NumSeverities = 1;
            filter.DenyList.pSeverityList = denied_severities;

            DXGI_INFO_QUEUE_MESSAGE_ID denied_message_ids[] = {1016};
            filter.DenyList.NumIDs = 1;
            filter.DenyList.pIDList = denied_message_ids;

            // doesn't work, see above
            PHI_D3D12_VERIFY(mInfoQueue->PushStorageFilter(DXGI_DEBUG_ALL, &filter));
            // none of these work either:
            // PHI_D3D12_VERIFY(mInfoQueue->PushDenyAllStorageFilter(DXGI_DEBUG_D3D12));
            // PHI_D3D12_VERIFY(mInfoQueue->PushDenyAllRetrievalFilter(DXGI_DEBUG_D3D12));
            // mInfoQueue->SetMuteDebugOutput(DXGI_DEBUG_D3D12, TRUE);
        }
    }
#endif

    // Factory init
    {
#ifdef PHI_HAS_OPTICK
        OPTICK_EVENT("IDXGIFactory Create");
#endif

        shared_com_ptr<IDXGIFactory> tempFactory;
        PHI_D3D12_VERIFY(::CreateDXGIFactory(PHI_COM_WRITE(tempFactory)));
        PHI_D3D12_VERIFY(tempFactory->QueryInterface(IID_PPV_ARGS(&mFactory)));
    }


    // Debug layer init
    // NOTE: This must come BEFORE D3D12Device creation!
    // if not, there is a silent device removal afterwards
    if (config.validation != validation_level::off)
    {
#ifdef PHI_HAS_OPTICK
        OPTICK_EVENT("Debug Layer Init");
#endif

        shared_com_ptr<ID3D12Debug> debugController;
        bool const wasDebugInitSuccessful = detail::hr_succeeded(::D3D12GetDebugInterface(PHI_COM_WRITE(debugController)));

        if (wasDebugInitSuccessful && debugController.is_valid())
        {
            debugController->EnableDebugLayer();

            if (config.validation >= validation_level::on_extended)
            {
                shared_com_ptr<ID3D12Debug3> debugControllerV3;
                bool const wasGBVInitSuccessful = detail::hr_succeeded(debugController.get_interface(debugControllerV3));

                if (wasGBVInitSuccessful && debugControllerV3.is_valid())
                {
                    debugControllerV3->SetEnableGPUBasedValidation(true);

                    // TODO: even if this succeeded, we could have still
                    // launched from inside NSight, where SetEnableSynchronizedCommandQueueValidation
                    // will crash
                    debugControllerV3->SetEnableSynchronizedCommandQueueValidation(true);
                }
                else
                {
                    PHI_LOG_ERROR("failed to enable GPU-based validation");
                }
            }
        }
        else
        {
            // (prevent clangf from breaking the URL in code)
            // clang-format off
            PHI_LOG_ERROR ("failed to enable D3D12 validation\n"
                           "  verify that the D3D12 SDK is installed on this machine\n"
                           "  refer to "
                           "https://docs.microsoft.com/en-us/windows/uwp/gaming/use-the-directx-runtime-and-visual-studio-graphics-diagnostic-features");
            // clang-format on
        }
    }

    // Adapter init
    {
#ifdef PHI_HAS_OPTICK
        OPTICK_EVENT("GPU Choice");
#endif

        phi::gpu_info candidates[16];

        phi::gpu_info const* chosenCandidate = nullptr;
        uint32_t numCandidates = 0;

        IDXGIAdapter* chosenAdapter = nullptr;
        ID3D12Device* chosenDevice = nullptr;

        if (config.adapter == adapter_preference::first)
        {
            // fast-path, do not create all D3D12 devices
            uint32_t adapterIndex = 0;
            bool success = getFirstAdapter(mFactory, &chosenAdapter, &chosenDevice, &adapterIndex);

            if (!success)
            {
                PHI_LOG_ASSERT("Fatal: Found no GPU");
                return false;
            }

            candidates[0] = getAdapterInfo(chosenAdapter, adapterIndex);
            chosenCandidate = &candidates[0];
            numCandidates = 1;
        }
        else
        {
            // query and create all GPUs, then choose based on preference

            ID3D12Device* candidateDevices[16] = {};
            IDXGIAdapter* candidateAdapters[16] = {};

            // choose the adapter
            numCandidates = getAdapterCandidates(mFactory, candidates, candidateDevices, candidateAdapters);

            if (numCandidates == 0)
            {
                PHI_LOG_ASSERT("Fatal: Found no GPU candidates");
                return false;
            }

            cc::span<phi::gpu_info> const candidateSpan = cc::span(candidates, numCandidates);

            // indexing into candidates[], candidateDevices[], candidateAdapters[]
            size_t chosenCandidateIndex = 0;
            if (config.adapter == adapter_preference::explicit_index)
            {
                // indexing into D3Ds adapters (used in IDXGIFactory::EnumAdapters)
                uint32_t const chosenD3DAdapterIndex = config.explicit_adapter_index;

                bool hasFoundExplicitIndex = false;
                for (auto i = 0u; i < numCandidates; ++i)
                {
                    if (candidateSpan[i].index == chosenD3DAdapterIndex)
                    {
                        hasFoundExplicitIndex = true;
                        chosenCandidateIndex = i;
                        break;
                    }
                }

                if (!hasFoundExplicitIndex)
                {
                    PHI_LOG_ASSERT("Fatal: Failed to find given explicit adapter (GPU) index");
                    return false;
                }
            }
            else
            {
                chosenCandidateIndex = getPreferredGPU(candidateSpan, config.adapter);

                if (chosenCandidateIndex >= numCandidates)
                {
                    PHI_LOG_ASSERT("Fatal: Found no GPU candidates");
                    return false;
                }
            }

            chosenCandidate = &candidateSpan[chosenCandidateIndex];
            chosenAdapter = candidateAdapters[chosenCandidateIndex];
            chosenDevice = candidateDevices[chosenCandidateIndex];

            // release all the others
            for (auto i = 0u; i < numCandidates; ++i)
            {
                if (i == chosenCandidateIndex)
                    continue;

                candidateDevices[i]->Release();
                candidateAdapters[i]->Release();
            }
        }

        // detect intel GPUs for GBV warning
        bool const isIntelGPU = chosenCandidate->vendor == gpu_vendor::intel;
        if (isIntelGPU && config.validation >= validation_level::on_extended)
        {
            PHI_LOG_WARN("GPU-based validation requested on an Intel GPU");
            PHI_LOG_WARN("There are known crashes in this configuration, consider disabling it");
        }

        // print the startup message
        printStartupMessage(numCandidates, chosenCandidate, config, true);

        // QI the real adapter pointer, release the temp one
        {
            PHI_D3D12_VERIFY(chosenAdapter->QueryInterface(IID_PPV_ARGS(&mAdapter)));
            chosenAdapter->Release();
        }

        // write the chosen ID3D12Device* to the out param
        outCreatedDevice = chosenDevice;

        mGPUInfo = *chosenCandidate;
    }

    return true;
}

void phi::d3d12::Adapter::destroy()
{
    PHI_D3D12_SAFE_RELEASE(mAdapter);
    PHI_D3D12_SAFE_RELEASE(mFactory);
    PHI_D3D12_SAFE_RELEASE(mInfoQueue);
}
