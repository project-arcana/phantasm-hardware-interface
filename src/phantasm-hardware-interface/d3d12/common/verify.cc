#include "verify.hh"

#include <cstdio>
#include <cstdlib>

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/common/log.hh>

#include "d3d12_sanitized.hh"
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
        PHI_D3D12_BC_SWITCH_RETURN(DISPATCHMESH);
    default:
        return "Unknown Operation";
    }

    // formatted from d3d12 enum using
    // Find: D3D12_AUTO_BREADCRUMB_OP_([a-z,0-9]*)	= ([0-9]*),
    // Replace: PHI_D3D12_BC_SWITCH_RETURN(\1);
#undef PHI_D3D12_BC_SWITCH_RETURN
}

/// outputs a HRESULT's error message to a buffer, returns amount of characters
DWORD get_hresult_error_message(HRESULT error_code, char* out_string, DWORD out_length)
{
    // this is a more verbose way of calling _com_error(hr).ErrorMessage(), made for two reasons:
    // 1. FORMAT_MESSAGE_MAX_WIDTH_MASK strips the \r symbol from the string
    // 2. The language can be forced to english with the fourth arg (MAKELANGID(...))
    return FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK, nullptr, error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), // always english
                          out_string, out_length, nullptr);
}

void print_dred_information(ID3D12Device* device)
{
    using namespace phi::d3d12;

    HRESULT removal_reason = device->GetDeviceRemovedReason();

    char removal_reason_string[1024];
    removal_reason_string[0] = '\0';
    get_hresult_error_message(removal_reason, removal_reason_string, sizeof(removal_reason_string));

    PHI_LOG_ASSERT("device removal reason:");
    PHI_LOG_ASSERT(R"(  "{}")", static_cast<char const*>(removal_reason_string));

    phi::d3d12::shared_com_ptr<ID3D12DeviceRemovedExtendedData> dred;
    if (SUCCEEDED(device->QueryInterface(PHI_COM_WRITE(dred))))
    {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT DredAutoBreadcrumbsOutput;
        D3D12_DRED_PAGE_FAULT_OUTPUT DredPageFaultOutput;
        auto hr1 = dred->GetAutoBreadcrumbsOutput(&DredAutoBreadcrumbsOutput);
        auto hr2 = dred->GetPageFaultAllocationOutput(&DredPageFaultOutput);

        if (SUCCEEDED(hr1))
        {
            D3D12_AUTO_BREADCRUMB_NODE const* bc_node = DredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
            auto num_breadcrumbs = 0u;
            while (bc_node != nullptr && num_breadcrumbs < 10)
            {
                PHI_LOG_ASSERT("node #{} ({} breadcrumbs)", num_breadcrumbs, bc_node->BreadcrumbCount);

                if (bc_node->pCommandListDebugNameA != nullptr)
                    PHI_LOG_ASSERT("  on list \"{}\"", bc_node->pCommandListDebugNameA);

                if (bc_node->pCommandQueueDebugNameA != nullptr)
                    PHI_LOG_ASSERT("  on queue \"{}\"", bc_node->pCommandQueueDebugNameA);


                auto logger = PHI_LOG_ASSERT;
                logger << "    ";

                unsigned const last_executed_i = *bc_node->pLastBreadcrumbValue;
                for (auto i = 0u; i < bc_node->BreadcrumbCount; ++i)
                {
                    if (i == last_executed_i)
                        logger << "[[>  " << get_breadcrumb_op_literal(bc_node->pCommandHistory[i]) << " <]]";
                    else
                        logger << "[" << get_breadcrumb_op_literal(bc_node->pCommandHistory[i]) << "]";
                }
                if (last_executed_i == bc_node->BreadcrumbCount)
                    logger << "  (fully executed)";
                else
                    logger << "  (last executed: " << last_executed_i << ")";

                bc_node = bc_node->pNext;
                ++num_breadcrumbs;
            }
            PHI_LOG_ASSERT << "end of breadcrumb data";
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

            PHI_LOG_ASSERT << "end of pagefault data";
        }

        if (FAILED(hr1) || FAILED(hr2))
        {
            PHI_LOG_ASSERT("Some DRED queries failed, use validation_level::on_extended_dred for more information after device removals");
        }
    }
    else
    {
        PHI_LOG_ASSERT("Some DRED queries failed, use validation_level::on_extended_dred for more information after device removals");
    }
}

void show_error_alert_box(char const* /*expression*/, char const* error, char const* filename, int line)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "Fatal D3D12 error:\n\n%s\n\nFile:\n%s:%d", error, filename, line);

    ::MessageBeep(MB_ICONERROR);
    ::MessageBoxA(nullptr, buf, "PHI D3D12 Error", MB_OK | MB_ICONERROR);
}
}


void phi::d3d12::detail::verify_failure_handler(HRESULT hr, const char* expression, const char* filename, int line, ID3D12Device* device)
{
    // Make sure this really is a failed HRESULT
    CC_RUNTIME_ASSERT(FAILED(hr) && "assert handler was called with a non-failed HRESULT");

    char error_string[1024];
    error_string[0] = '\0';
    get_hresult_error_message(hr, error_string, sizeof(error_string));

    PHI_LOG_ASSERT("D3D12 call {} failed", expression);
    PHI_LOG_ASSERT("  error:");
    PHI_LOG_ASSERT("    \"{}\"", static_cast<char const*>(error_string));
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

    // TODO: Graceful shutdown
    std::abort();
}

void phi::d3d12::detail::dred_assert_handler(void* device_child, const char* expression, const char* filename, int line)
{
    PHI_LOG_ASSERT("device-removal related assert on {} failed", expression);
    PHI_LOG_ASSERT("  file {}:{}", filename, line);

    auto* const as_device_child = static_cast<ID3D12DeviceChild*>(device_child);

    shared_com_ptr<ID3D12Device> recovered_device;
    auto const hr = as_device_child->GetDevice(PHI_COM_WRITE(recovered_device));
    if (hr_succeeded(hr) && recovered_device.is_valid())
    {
        print_dred_information(recovered_device);
    }
    else
    {
        PHI_LOG_ASSERT("Failed to recover device from ID3D12DeviceChild {} (error: {})", device_child, static_cast<char const*>(_com_error(hr).Description()));
    }

    show_error_alert_box(expression, "DRED Assert (device removed)", filename, line);

    // TODO: Graceful shutdown
    std::abort();
}
