#include "Device.hh"

#include "adapter_choice_util.hh"
#include "common/d3d12_sanitized.hh"
#include "common/log.hh"
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
            log::err() << "warning: failed to enable DRED";
        }
    }

    PHI_D3D12_VERIFY(::D3D12CreateDevice(&adapter, D3D_FEATURE_LEVEL_12_0, PHI_COM_WRITE(mDevice)));

    // Capability checks
    mFeatures = check_capabilities(mDevice);

    // QIs
    auto const got_device5 = SUCCEEDED(mDevice->QueryInterface(PHI_COM_WRITE(mDevice5)));
    if (!got_device5)
    {
        // this is likely redundant, but just to make sure
        mDevice5 = nullptr;
    }
}
