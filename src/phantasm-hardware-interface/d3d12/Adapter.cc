#include "Adapter.hh"

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/config.hh>
#include <phantasm-hardware-interface/features/gpu_info.hh>

#include "adapter_choice_util.hh"
#include "common/d3d12_sanitized.hh"
#include "common/verify.hh"

void phi::d3d12::Adapter::initialize(const backend_config& config)
{
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
        shared_com_ptr<IDXGIFactory> temp_factory;
        PHI_D3D12_VERIFY(::CreateDXGIFactory(PHI_COM_WRITE(temp_factory)));
        PHI_D3D12_VERIFY(temp_factory->QueryInterface(IID_PPV_ARGS(&mFactory)));
    }

    // Adapter init
    bool is_intel_gpu = false;
    {
        // choose the adapter
        auto const candidates = get_adapter_candidates();
        auto const chosen_adapter_index = config.adapter == adapter_preference::explicit_index
                                              ? config.explicit_adapter_index
                                              : candidates[get_preferred_gpu(candidates, config.adapter)].index;

        CC_RUNTIME_ASSERT(chosen_adapter_index != uint32_t(-1));

        // detect intel GPUs for GBV workaround
        for (auto const& candidate : candidates)
        {
            if (candidate.index == chosen_adapter_index)
            {
                is_intel_gpu = candidate.vendor == gpu_vendor::intel;
                break;
            }
        }

        // create the adapter
        shared_com_ptr<IDXGIAdapter> temp_adapter;
        mFactory->EnumAdapters(chosen_adapter_index, temp_adapter.override());
        PHI_D3D12_VERIFY(temp_adapter->QueryInterface(IID_PPV_ARGS(&mAdapter)));

        print_startup_message(candidates, chosen_adapter_index, config, true);
    }

    // Debug layer init
    if (config.validation != validation_level::off)
    {
        shared_com_ptr<ID3D12Debug> debug_controller;
        bool const debug_init_success = detail::hr_succeeded(::D3D12GetDebugInterface(PHI_COM_WRITE(debug_controller)));


        if (debug_init_success && debug_controller.is_valid())
        {
            debug_controller->EnableDebugLayer();

            if (config.validation >= validation_level::on_extended)
            {
                if (is_intel_gpu)
                {
                    PHI_LOG_WARN("GPU-based validation requested on an Intel GPU, disabling due to known crashes");
                }
                else
                {
                    shared_com_ptr<ID3D12Debug3> debug_controller_v3;
                    bool const gbv_init_success = detail::hr_succeeded(debug_controller.get_interface(debug_controller_v3));

                    if (gbv_init_success && debug_controller_v3.is_valid())
                    {
                        debug_controller_v3->SetEnableGPUBasedValidation(true);

                        // TODO: even if this succeeded, we could have still
                        // launched from inside NSight, where SetEnableSynchronizedCommandQueueValidation
                        // will crash
                        debug_controller_v3->SetEnableSynchronizedCommandQueueValidation(true);
                    }
                    else
                    {
                        PHI_LOG_ERROR << "failed to enable GPU-based validation";
                    }
                }
            }
        }
        else
        {
            PHI_LOG_ERROR << "failed to enable validation\n"
                             "  verify that the D3D12 SDK is installed on this machine\n"
                             "  refer to "
                             "https://docs.microsoft.com/en-us/windows/uwp/gaming/"
                             "use-the-directx-runtime-and-visual-studio-graphics-diagnostic-features";
        }
    }
}

void phi::d3d12::Adapter::destroy()
{
    PHI_D3D12_SAFE_RELEASE(mAdapter);
    PHI_D3D12_SAFE_RELEASE(mFactory);
    PHI_D3D12_SAFE_RELEASE(mInfoQueue);
}
