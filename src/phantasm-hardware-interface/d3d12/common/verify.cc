#include "verify.hh"

#include <cstdio>
#include <cstdlib>

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/common/log.hh>

#include "d3d12_sanitized.hh"
#include "sdk_version.hh"
#include "shared_com_ptr.hh"

namespace
{
char const* get_breadcrumb_op_literal(D3D12_AUTO_BREADCRUMB_OP op)
{
#define PHI_D3D12_BC_SWITCH_RETURN(_opname_)  \
    case D3D12_AUTO_BREADCRUMB_OP_##_opname_: \
        return #_opname_

    switch (op)
    {
        PHI_D3D12_BC_SWITCH_RETURN(SETMARKER);
        PHI_D3D12_BC_SWITCH_RETURN(BEGINEVENT);
        PHI_D3D12_BC_SWITCH_RETURN(ENDEVENT);
        PHI_D3D12_BC_SWITCH_RETURN(DRAWINSTANCED);
        PHI_D3D12_BC_SWITCH_RETURN(DRAWINDEXEDINSTANCED);
        PHI_D3D12_BC_SWITCH_RETURN(EXECUTEINDIRECT);
        PHI_D3D12_BC_SWITCH_RETURN(DISPATCH);
        PHI_D3D12_BC_SWITCH_RETURN(COPYBUFFERREGION);
        PHI_D3D12_BC_SWITCH_RETURN(COPYTEXTUREREGION);
        PHI_D3D12_BC_SWITCH_RETURN(COPYRESOURCE);
        PHI_D3D12_BC_SWITCH_RETURN(COPYTILES);
        PHI_D3D12_BC_SWITCH_RETURN(RESOLVESUBRESOURCE);
        PHI_D3D12_BC_SWITCH_RETURN(CLEARRENDERTARGETVIEW);
        PHI_D3D12_BC_SWITCH_RETURN(CLEARUNORDEREDACCESSVIEW);
        PHI_D3D12_BC_SWITCH_RETURN(CLEARDEPTHSTENCILVIEW);
        PHI_D3D12_BC_SWITCH_RETURN(RESOURCEBARRIER);
        PHI_D3D12_BC_SWITCH_RETURN(EXECUTEBUNDLE);
        PHI_D3D12_BC_SWITCH_RETURN(PRESENT);
        PHI_D3D12_BC_SWITCH_RETURN(RESOLVEQUERYDATA);
        PHI_D3D12_BC_SWITCH_RETURN(BEGINSUBMISSION);
        PHI_D3D12_BC_SWITCH_RETURN(ENDSUBMISSION);
        PHI_D3D12_BC_SWITCH_RETURN(DECODEFRAME);
        PHI_D3D12_BC_SWITCH_RETURN(PROCESSFRAMES);
        PHI_D3D12_BC_SWITCH_RETURN(ATOMICCOPYBUFFERUINT);
        PHI_D3D12_BC_SWITCH_RETURN(ATOMICCOPYBUFFERUINT64);
        PHI_D3D12_BC_SWITCH_RETURN(RESOLVESUBRESOURCEREGION);
        PHI_D3D12_BC_SWITCH_RETURN(WRITEBUFFERIMMEDIATE);
        PHI_D3D12_BC_SWITCH_RETURN(DECODEFRAME1);
        PHI_D3D12_BC_SWITCH_RETURN(SETPROTECTEDRESOURCESESSION);
        PHI_D3D12_BC_SWITCH_RETURN(DECODEFRAME2);
        PHI_D3D12_BC_SWITCH_RETURN(PROCESSFRAMES1);
        PHI_D3D12_BC_SWITCH_RETURN(BUILDRAYTRACINGACCELERATIONSTRUCTURE);
        PHI_D3D12_BC_SWITCH_RETURN(EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO);
        PHI_D3D12_BC_SWITCH_RETURN(COPYRAYTRACINGACCELERATIONSTRUCTURE);
        PHI_D3D12_BC_SWITCH_RETURN(DISPATCHRAYS);
        PHI_D3D12_BC_SWITCH_RETURN(INITIALIZEMETACOMMAND);
        PHI_D3D12_BC_SWITCH_RETURN(EXECUTEMETACOMMAND);
        PHI_D3D12_BC_SWITCH_RETURN(ESTIMATEMOTION);
        PHI_D3D12_BC_SWITCH_RETURN(RESOLVEMOTIONVECTORHEAP);
        PHI_D3D12_BC_SWITCH_RETURN(SETPIPELINESTATE1);
        PHI_D3D12_BC_SWITCH_RETURN(INITIALIZEEXTENSIONCOMMAND);
        PHI_D3D12_BC_SWITCH_RETURN(EXECUTEEXTENSIONCOMMAND);
#if PHI_D3D12_HAS_20H1_FEATURES
        PHI_D3D12_BC_SWITCH_RETURN(DISPATCHMESH);
#endif

    default:
        return "Unknown Operation";
    }

    // formatted from d3d12 enum using
    // Find: D3D12_AUTO_BREADCRUMB_OP_([a-z,0-9]*)	= ([0-9]*),
    // Replace: PHI_D3D12_BC_SWITCH_RETURN(\1);
#undef PHI_D3D12_BC_SWITCH_RETURN
}

char const* get_hresult_literal(HRESULT hr)
{
#define PHI_D3D12_HR_SWITCH_RETURN(_opname_) \
    case _opname_:                           \
        return #_opname_

    switch (hr)
    {
        // common win32
        PHI_D3D12_HR_SWITCH_RETURN(E_UNEXPECTED);
        PHI_D3D12_HR_SWITCH_RETURN(E_NOTIMPL);
        PHI_D3D12_HR_SWITCH_RETURN(E_OUTOFMEMORY);
        PHI_D3D12_HR_SWITCH_RETURN(E_INVALIDARG);
        PHI_D3D12_HR_SWITCH_RETURN(E_NOINTERFACE);
        PHI_D3D12_HR_SWITCH_RETURN(E_POINTER);
        PHI_D3D12_HR_SWITCH_RETURN(E_HANDLE);
        PHI_D3D12_HR_SWITCH_RETURN(E_ABORT);
        PHI_D3D12_HR_SWITCH_RETURN(E_FAIL);
        PHI_D3D12_HR_SWITCH_RETURN(E_ACCESSDENIED);
        PHI_D3D12_HR_SWITCH_RETURN(E_PENDING);
        PHI_D3D12_HR_SWITCH_RETURN(E_BOUNDS);
        PHI_D3D12_HR_SWITCH_RETURN(E_CHANGED_STATE);
        PHI_D3D12_HR_SWITCH_RETURN(E_ILLEGAL_STATE_CHANGE);
        PHI_D3D12_HR_SWITCH_RETURN(S_FALSE);

        // d3d12
        PHI_D3D12_HR_SWITCH_RETURN(D3D12_ERROR_ADAPTER_NOT_FOUND);
        PHI_D3D12_HR_SWITCH_RETURN(D3D12_ERROR_DRIVER_VERSION_MISMATCH);
        // PHI_D3D12_HR_SWITCH_RETURN(D3DERR_INVALIDCALL);
        // PHI_D3D12_HR_SWITCH_RETURN(D3DERR_WASSTILLDRAWING);

        // dxgi
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_ACCESS_DENIED);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_ACCESS_LOST);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_ALREADY_EXISTS);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_DEVICE_HUNG);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_DEVICE_REMOVED);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_DEVICE_RESET);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_DRIVER_INTERNAL_ERROR);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_FRAME_STATISTICS_DISJOINT);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_INVALID_CALL);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_MORE_DATA);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_NAME_ALREADY_EXISTS);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_NONEXCLUSIVE);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_NOT_FOUND);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_REMOTE_OUTOFMEMORY);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_SDK_COMPONENT_MISSING);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_SESSION_DISCONNECTED);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_UNSUPPORTED);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_WAIT_TIMEOUT);
        PHI_D3D12_HR_SWITCH_RETURN(DXGI_ERROR_WAS_STILL_DRAWING);

    default:
        return "[unrecognized HRESULT code]";
    }

#undef PHI_D3D12_HR_SWITCH_RETURN
}

/// outputs a HRESULT's error message to a buffer, returns amount of characters
DWORD get_hresult_error_message(HRESULT error_code, char* out_string, DWORD out_length)
{
    // this is a more verbose way of calling _com_error(hr).ErrorMessage(), made for two reasons:
    // 1. FORMAT_MESSAGE_MAX_WIDTH_MASK strips the \r symbol from the string
    // 2. The language can be forced to english with the fourth arg (MAKELANGID(...))
    //      HOWEVER, under some circumstances this requires a loaded MUI file which is unlikely in general
    //      thus, use zero

    auto const res = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK, nullptr, error_code, 0, out_string, out_length, nullptr);

    if (res == 0)
    {
        PHI_LOG_ASSERT("FormatMessageA failed: {}", uint32_t(GetLastError()));
    }

    return res;
}

void print_dred_information(ID3D12Device* device)
{
    using namespace phi::d3d12;

    {
        HRESULT removal_reason = device->GetDeviceRemovedReason();

        char buf[1024];
        buf[0] = '\0';
        get_hresult_error_message(removal_reason, buf, sizeof(buf));

        PHI_LOG_ASSERT("Device was removed for the following reason:");
        PHI_LOG_ASSERT(R"(  {}: "{}")", get_hresult_literal(removal_reason), static_cast<char const*>(buf));
    }

    bool didAnyQueriesFail = false;
    phi::d3d12::shared_com_ptr<ID3D12DeviceRemovedExtendedData> dred;
    HRESULT hrQI = device->QueryInterface(PHI_COM_WRITE(dred));
    if (SUCCEEDED(hrQI))
    {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT DredAutoBreadcrumbsOutput;
        D3D12_DRED_PAGE_FAULT_OUTPUT DredPageFaultOutput;
        auto hr1 = dred->GetAutoBreadcrumbsOutput(&DredAutoBreadcrumbsOutput);
        auto hr2 = dred->GetPageFaultAllocationOutput(&DredPageFaultOutput);

        if (SUCCEEDED(hr1))
        {
            PHI_LOG_ASSERT("");
            PHI_LOG_ASSERT("DRED breadcrumbs:");

            D3D12_AUTO_BREADCRUMB_NODE const* bc_node = DredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
            auto num_breadcrumbs = 0u;
            while (bc_node != nullptr && num_breadcrumbs < 10)
            {
                PHI_LOG_ASSERT("node #{} ({} breadcrumbs)", num_breadcrumbs, bc_node->BreadcrumbCount);

                if (bc_node->pCommandListDebugNameA != nullptr)
                    PHI_LOG_ASSERT("  on list \"{}\"", bc_node->pCommandListDebugNameA);

                if (bc_node->pCommandQueueDebugNameA != nullptr)
                    PHI_LOG_ASSERT("  on queue \"{}\"", bc_node->pCommandQueueDebugNameA);

                cc::string logger = "    ";

                unsigned const last_executed_i = *bc_node->pLastBreadcrumbValue;
                unsigned num_logged_contiguous = 0;
                for (auto i = 0u; i < bc_node->BreadcrumbCount; ++i)
                {
                    if (num_logged_contiguous == 6)
                    {
                        logger += ",\n                                          ";
                        num_logged_contiguous = 0;
                    }

                    if (i == last_executed_i)
                        logger += "[[> " + cc::string(get_breadcrumb_op_literal(bc_node->pCommandHistory[i])) + " <]] ";
                    else
                        logger += "[" + cc::string(get_breadcrumb_op_literal(bc_node->pCommandHistory[i])) + "] ";
                    ++num_logged_contiguous;
                }

                if (last_executed_i == bc_node->BreadcrumbCount)
                    logger += "  (fully executed)";
                else
                    logger += "  (execution halted at #" + cc::to_string(last_executed_i) + ")";

                bc_node = bc_node->pNext;
                ++num_breadcrumbs;

                PHI_LOG_ASSERT("{}", logger.c_str());
            }

            PHI_LOG_ASSERT("end of breadcrumb data");
        }
        else
        {
            didAnyQueriesFail = true;
            PHI_LOG_ASSERT("Failed to query DRED breadcrumbs (Called ID3D12DeviceRemovedExtendedData::GetAutoBreadcrumbsOutput):");

            char buf[1024];
            buf[0] = '\0';
            get_hresult_error_message(hr1, buf, sizeof(buf));
            PHI_LOG_ASSERT("  {}: \"{}\"", get_hresult_literal(hr1), static_cast<char const*>(buf));
        }

        if (SUCCEEDED(hr2))
        {
            PHI_LOG_ASSERT("pagefault VA: {}", DredPageFaultOutput.PageFaultVA);

            D3D12_DRED_ALLOCATION_NODE const* freed_node = DredPageFaultOutput.pHeadRecentFreedAllocationNode;
            while (freed_node != nullptr)
            {
                if (freed_node->ObjectNameA)
                    PHI_LOG_ASSERT("recently freed: {}", freed_node->ObjectNameA);

                freed_node = freed_node->pNext;
            }

            D3D12_DRED_ALLOCATION_NODE const* allocated_node = DredPageFaultOutput.pHeadExistingAllocationNode;
            while (allocated_node != nullptr)
            {
                if (allocated_node->ObjectNameA)
                    PHI_LOG_ASSERT("allocated: {}", allocated_node->ObjectNameA);

                allocated_node = allocated_node->pNext;
            }

            PHI_LOG_ASSERT("end of pagefault data");
        }
        else
        {
            didAnyQueriesFail = true;
            PHI_LOG_ASSERT("Failed to query DRED pagefault data (Called ID3D12DeviceRemovedExtendedData::GetPageFaultAllocationOutput):");

            char buf[1024];
            buf[0] = '\0';
            get_hresult_error_message(hr2, buf, sizeof(buf));
            PHI_LOG_ASSERT("  {}: \"{}\"", get_hresult_literal(hr2), static_cast<char const*>(buf));
        }
    }
    else
    {
        didAnyQueriesFail = true;

        char buf[1024];
        buf[0] = '\0';
        get_hresult_error_message(hrQI, buf, sizeof(buf));

        PHI_LOG_ASSERT("Failed to QI ID3D12DeviceRemovedExtendedData from ID3D12Device");
        PHI_LOG_ASSERT("  error: {}: \"{}\"", get_hresult_literal(hrQI), static_cast<char const*>(buf));
    }

    if (didAnyQueriesFail)
    {
        PHI_LOG_ASSERT("DRED queries failed, verify if validation_level::on_extended_dred is enabled for more information after device removals");
    }
}

void show_error_alert_box(char const* /*expression*/, char const* error, char const* filename, int line)
{
#if defined(_DEBUG)
    if (!IsDebuggerPresent())
    {
        // MessageBeep(MB_ICONERROR);
        _CrtDbgReport(_CRT_ERROR, filename, line, "phantasm-hardware-interface.dll", "%s", error);
        // we only survive this call if Retry or Ignore was clicked
        // let control run into the __debugbreak next
    }
#endif // CC_OS_WINDOWS && _DEBUG
}
} // namespace


void phi::d3d12::detail::verify_failure_handler(HRESULT hr, const char* expression, const char* filename, int line, ID3D12Device* device)
{
    // Make sure this really is a failed HRESULT
    CC_RUNTIME_ASSERT(FAILED(hr) && "assert handler was called with a non-failed HRESULT");

    char error_string[1024];
    error_string[0] = '\0';
    get_hresult_error_message(hr, error_string, sizeof(error_string));

    PHI_LOG_ASSERT("D3D12 call {} failed", expression);
    PHI_LOG_ASSERT("  error:");
    PHI_LOG_ASSERT("    {}: \"{}\"", get_hresult_literal(hr), static_cast<char const*>(error_string));
    PHI_LOG_ASSERT("  in file {}:{}", filename, line);

    if (hr == DXGI_ERROR_DEVICE_REMOVED && device)
    {
        if (device != nullptr)
        {
            print_dred_information(device);
        }
        else
        {
            PHI_LOG_ASSERT("device was removed, but assert handler has no access to ID3D12Device");
        }
    }

    show_error_alert_box(expression, error_string, filename, line);
}

void phi::d3d12::detail::dred_assert_handler(void* device_child, const char* expression, const char* filename, int line)
{
    PHI_LOG_ASSERT("device removed - assert on {} failed", expression);
    PHI_LOG_ASSERT("  in file {}:{}", filename, line);

    auto* const as_device_child = static_cast<ID3D12DeviceChild*>(device_child);

    shared_com_ptr<ID3D12Device> recovered_device;
    auto const hr = as_device_child->GetDevice(PHI_COM_WRITE(recovered_device));
    if (hr_succeeded(hr) && recovered_device.is_valid())
    {
        print_dred_information(recovered_device);
    }
    else
    {
        char error_string[1024];
        error_string[0] = '\0';
        get_hresult_error_message(hr, error_string, sizeof(error_string));
        PHI_LOG_ASSERT("Failed to recover device from ID3D12DeviceChild {}", device_child);
        PHI_LOG_ASSERT("  error:");
        PHI_LOG_ASSERT("    {}: \"{}\"", get_hresult_literal(hr), static_cast<char const*>(error_string));
    }

    show_error_alert_box(expression, "DRED Assert - Device Removed", filename, line);
}
