#include "Device.hh"

#include <cstdio>

#include <clean-core/native/win32_util.hh>

#include <phantasm-hardware-interface/common/log.hh>

#include "adapter_choice_util.hh"
#include "common/d3d12_sanitized.hh"
#include "common/safe_seh_call.hh"
#include "common/shared_com_ptr.hh"
#include "common/verify.hh"


void phi::d3d12::Device::initialize(IDXGIAdapter& adapter, const backend_config& config)
{
    // shutdown crash detection / workaround
    {
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
            PHI_LOG_ERROR << "failed to enable DRED";
        }
    }

    // Device Creation
    shared_com_ptr<ID3D12Device> temp_device;
    PHI_D3D12_VERIFY(::D3D12CreateDevice(&adapter, D3D_FEATURE_LEVEL_12_0, PHI_COM_WRITE(temp_device)));

    // GBV startup message
    if (config.validation >= validation_level::on_extended)
    {
        // D3D12 has just logged its GPU validation startup message, print a newline
        // to make following errors more legible
        // this also allows the user to verify if validation layer messages are printed on the TTY he's looking at,
        // it can for example instead be printed to the VS debug console
        PHI_LOG("gpu validation enabled \u001b[38;5;244m(if there is no message above ^^^^ d3d12 is printing to a different tty, like the vs debug "
                "console)\u001b[0m");
        std::printf("\n");
        std::fflush(stdout);
    }

    // QI proper device
    auto const got_device5 = SUCCEEDED(temp_device->QueryInterface(IID_PPV_ARGS(&mDevice)));
    if (!got_device5)
    {
        // there is no way to recover here
        // Device5 support is purely OS-based, Win10 1809+, aka Redstone 5
        PHI_LOG_ASSERT("fatal error: unable to QI ID3D12Device5 - please update to Windows 10 1809 or higher");
        PHI_LOG_ASSERT("to check your windows version, press Win + R and enter 'winver'");
        CC_RUNTIME_ASSERT(false && "unsupported windows 10 version, please update to windows 10 1809 or higher");
    }

    // Feature checks
    mFeatures = get_gpu_features(mDevice);
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
