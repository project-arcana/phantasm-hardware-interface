#include "Device.hh"

#include <cstdio>

#include <phantasm-hardware-interface/detail/log.hh>

#include "adapter_choice_util.hh"
#include "common/d3d12_sanitized.hh"
#include "common/verify.hh"


void phi::d3d12::Device::initialize(IDXGIAdapter& adapter, const backend_config& config)
{
    if (config.validation >= validation_level::on_extended_dred)
    {
        auto const hr = D3D12GetDebugInterface(PHI_COM_WRITE(mDREDSettings));

        if (detail::hr_succeeded(hr) && mDREDSettings.is_valid())
        {
            mDREDSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            mDREDSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            // mDREDSettings->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
        else
        {
            PHI_LOG_ERROR << "failed to enable DRED";
        }
    }

    PHI_D3D12_VERIFY(::D3D12CreateDevice(&adapter, D3D_FEATURE_LEVEL_12_0, PHI_COM_WRITE(mDevice)));

    if (config.validation >= validation_level::on_extended)
    {
        // D3D12 has just logged its GPU validation startup message, print a newline
        // to make following errors more legible
        // this also allows the user to verify if validation layer messages are printed on the TTY he's looking at,
        // it can for example instead be printed to the VS debug console
        PHI_LOG("gpu validation enabled \u001b[38;5;244m(if there is no message above ^^^^ d3d12 is printing to a different tty, like the vs debug console)\u001b[0m");
        std::printf("\n");
        std::fflush(stdout);
    }

    // Feature checks
    mFeatures = get_gpu_features(mDevice);

    // QIs
    auto const got_device5 = SUCCEEDED(mDevice->QueryInterface(PHI_COM_WRITE(mDevice5)));
    if (!got_device5)
    {
        PHI_LOG_ERROR << "unable to QI ID3D12Device5 - please update to Windows 10 1809 or higher";
        PHI_LOG_ERROR << "to check your windows version, press Win + R and enter 'winver'";
        CC_RUNTIME_ASSERT(false && "unsupported windows 10 version, please update to windows 10 1809 or higher");
        // this is likely redundant, but just to make sure
        mDevice5 = nullptr;
    }

    if ((config.native_features & backend_config::native_feature_d3d12_break_on_warn) != 0)
    {
        if (config.validation < validation_level::on)
        {
            PHI_LOG_ERROR("cannot enable d3d12_break_on_warn with disabled validation");
        }
        else
        {
            shared_com_ptr<ID3D12InfoQueue> info_queue;
            PHI_D3D12_VERIFY(mDevice5.get_interface(info_queue));
            PHI_D3D12_VERIFY(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE));
            PHI_LOG("d3d12_break_on_warn enabled");
        }
    }
}
