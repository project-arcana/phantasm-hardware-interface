#include "Device.hh"

#include <cstdio>

#ifdef PHI_HAS_OPTICK
#include <optick.h>
#endif

#include <clean-core/native/win32_util.hh>

#include <phantasm-hardware-interface/common/log.hh>

#include "adapter_choice_util.hh"
#include "common/d3d12_sanitized.hh"
#include "common/safe_seh_call.hh"
#include "common/shared_com_ptr.hh"
#include "common/verify.hh"


bool phi::d3d12::Device::initialize(ID3D12Device* deviceToUse, IDXGIAdapter& adapter, const backend_config& config)
{
#ifdef PHI_HAS_OPTICK
    OPTICK_EVENT();
#endif

    // shutdown crash detection / workaround
    {
#ifdef PHI_HAS_OPTICK
        OPTICK_EVENT("Shutdown Crash Detection");
#endif

        bool const is_shutdown_crash_workaround_requested
            = (config.native_features & backend_config::native_feature_d3d12_workaround_device_release_crash) != 0;

        // determine vulnerability to shutdown crash bug
        if (config.validation >= validation_level::on_extended)
        {
            // only affects enabled GBV
            unsigned winver_major, winver_minor, winver_build;
            if (cc::win32_get_version(winver_major, winver_minor, winver_build))
            {
                if (winver_major == 10 && winver_minor == 0 && winver_build <= 19042)
                {
                    // only affects windows versions up to and including 20H2

                    if (!is_shutdown_crash_workaround_requested)
                    {
                        PHI_LOG_WARN("the current windows version ({}.{}.{}) is affected by a spurious D3D12 crash at shutdown with enabled GPU "
                                     "based validation (validation::on_extended)",
                                     winver_major, winver_minor, winver_build);
                        PHI_LOG_WARN("it is resolved in releases after Win10 20H2, device destruction can be skipped by enabling "
                                     "d3d12_workaround_device_release_crash in the backend config native features");
                    }

                    mIsShutdownCrashSubsceptible = true;
                }
            }
        }

        if (is_shutdown_crash_workaround_requested)
        {
            if (mIsShutdownCrashSubsceptible)
            {
                mIsShutdownCrashWorkaroundActive = true;
                PHI_LOG("d3d12_workaround_device_release_crash enabled");
            }
            else
            {
                PHI_LOG_WARN("ignored d3d12_workaround_device_release_crash - not subsceptible");
            }
        }
    }

    // DRED
    if (config.validation >= validation_level::on_extended_dred)
    {
#ifdef PHI_HAS_OPTICK
        OPTICK_EVENT("DRED Init");
#endif

        shared_com_ptr<ID3D12DeviceRemovedExtendedDataSettings> dred_settings;
        auto const hr = D3D12GetDebugInterface(PHI_COM_WRITE(dred_settings));

        if (detail::hr_succeeded(hr) && dred_settings.is_valid())
        {
            dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            // dred_settings->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
        else
        {
            PHI_LOG_ERROR("failed to enable DRED");
        }
    }

    // Device Creation
    {
#ifdef PHI_HAS_OPTICK
        OPTICK_EVENT("ID3D12Device QI");
#endif

        // do not recreate a device here as device creation is a significant cost,
        // use the one created during GPU testing
        ID3D12Device* const temp_device = deviceToUse;

        // GBV startup message
        if (config.validation >= validation_level::on_extended)
        {
            // D3D12 has just logged its GPU validation startup message, print a newline
            // to make following errors more legible
            // this also allows the user to verify if validation layer messages are printed on the TTY he's looking at,
            // it can for example instead be printed to the VS debug console
            PHI_LOG("gpu validation enabled \u001b[38;5;244m(if there is no message above ^^^^ d3d12 is printing to a different tty, like the vs "
                    "debug "
                    "console)\u001b[0m");
            std::printf("\n");
            std::fflush(stdout);
        }

        // QI proper device
        bool const got_device5 = SUCCEEDED(temp_device->QueryInterface(IID_PPV_ARGS(&mDevice)));

        temp_device->Release();

        if (!got_device5)
        {
            // there is no way to recover here
            // Device5 support is purely OS-based, Win10 1809+, aka Redstone 5
            PHI_LOG_ERROR("fatal error: unable to QI ID3D12Device5 - please update to Windows 10 1809 or higher");
            PHI_LOG_ERROR("to check your windows version, press Win + R and enter 'winver'\n");
            return false;
        }
    }

    // Feature checks
    mFeatures = getGPUFeaturesFromDevice(mDevice);
    // "enable" raytracing if it's requested and the GPU is capable
    mIsRaytracingEnabled = config.enable_raytracing && mFeatures.raytracing >= gpu_feature_info::raytracing_t1_0;

    // break on warn
    if ((config.native_features & backend_config::native_feature_d3d12_break_on_warn) != 0)
    {
        if (config.validation < validation_level::on)
        {
            PHI_LOG_WARN("cannot enable d3d12_break_on_warn with disabled validation");
        }
        else
        {
            shared_com_ptr<ID3D12InfoQueue> info_queue;
            PHI_D3D12_VERIFY(mDevice->QueryInterface(PHI_COM_WRITE(info_queue)));
            PHI_D3D12_VERIFY(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE));
            PHI_D3D12_VERIFY(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
            PHI_D3D12_VERIFY(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
            PHI_LOG("d3d12_break_on_warn enabled");
        }
    }

    return true;
}

void phi::d3d12::Device::destroy()
{
    // print a warning about the spurious GBV shutdown crash in Win10 20H1 and 20H2
    if (mIsShutdownCrashSubsceptible)
    {
        if (!mIsShutdownCrashWorkaroundActive)
        {
            PHI_LOG("destroying ID3D12Device, spurious crash at shutdown might be imminent");
            PHI_LOG("device destruction can be skipped by enabling d3d12_workaround_device_release_crash in the backend config native features");

            PHI_D3D12_SAFE_RELEASE(mDevice);
        }
        else
        {
            // deliberately drop the device without destroying it
            PHI_LOG("d3d12_workaround_device_release_crash enabled - leaking ID3D12Device to avoid crash");
            mDevice = nullptr;
        }

        return;
    }

	

    PHI_D3D12_SAFE_RELEASE(mDevice);
}
