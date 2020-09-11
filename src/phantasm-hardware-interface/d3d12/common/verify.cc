#include "verify.hh"

#include <cstdio>
#include <cstdlib>

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/common/log.hh>

#include "d3d12_sanitized.hh"
#include "shared_com_ptr.hh"

namespace
{
#define CASE_STRINGIFY_RETURN(_val_) \
    case _val_:                      \
        return #_val_

char const* get_device_error_literal(HRESULT hr)
{
    switch (hr)
    {
        CASE_STRINGIFY_RETURN(DXGI_ERROR_DEVICE_HUNG);
        CASE_STRINGIFY_RETURN(DXGI_ERROR_DEVICE_REMOVED);
        CASE_STRINGIFY_RETURN(DXGI_ERROR_DEVICE_RESET);
        CASE_STRINGIFY_RETURN(DXGI_ERROR_DRIVER_INTERNAL_ERROR);
        CASE_STRINGIFY_RETURN(DXGI_ERROR_INVALID_CALL);
    default:
        return "Unknown Device Error";
    }
}

char const* get_general_error_literal(HRESULT hr)
{
    switch (hr)
    {
        CASE_STRINGIFY_RETURN(S_OK);
        CASE_STRINGIFY_RETURN(D3D11_ERROR_FILE_NOT_FOUND);
        CASE_STRINGIFY_RETURN(D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS);
        CASE_STRINGIFY_RETURN(E_FAIL);
        CASE_STRINGIFY_RETURN(E_INVALIDARG);
        CASE_STRINGIFY_RETURN(E_OUTOFMEMORY);
        CASE_STRINGIFY_RETURN(DXGI_ERROR_INVALID_CALL);
        CASE_STRINGIFY_RETURN(E_NOINTERFACE);
        CASE_STRINGIFY_RETURN(DXGI_ERROR_DEVICE_REMOVED);
    default:
        return "Unknown HRESULT";
    }
}

#undef CASE_STRINGIFY_RETURN

void print_dred_information(ID3D12Device* device)
{
    using namespace phi::d3d12;

    HRESULT removal_reason = device->GetDeviceRemovedReason();
    PHI_LOG_ASSERT << "device removal reason: " << get_device_error_literal(removal_reason);

    phi::d3d12::shared_com_ptr<ID3D12DeviceRemovedExtendedData> dred;
    if (SUCCEEDED(device->QueryInterface(PHI_COM_WRITE(dred))))
    {
        PHI_LOG_ASSERT << "DRED detected, querying outputs";
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT DredAutoBreadcrumbsOutput;
        D3D12_DRED_PAGE_FAULT_OUTPUT DredPageFaultOutput;
        auto hr1 = dred->GetAutoBreadcrumbsOutput(&DredAutoBreadcrumbsOutput);
        auto hr2 = dred->GetPageFaultAllocationOutput(&DredPageFaultOutput);

        //::DebugBreak();

        if (SUCCEEDED(hr1))
        {
            // TODO: Breadcrumb output
            D3D12_AUTO_BREADCRUMB_NODE const* breadcrumb = DredAutoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
            auto num_breadcrumbs = 0u;
            while (breadcrumb != nullptr && num_breadcrumbs < 10)
            {
                PHI_LOG_ASSERT("bc #{} size {}", num_breadcrumbs, breadcrumb->BreadcrumbCount);

                if (breadcrumb->pCommandListDebugNameA != nullptr)
                    PHI_LOG_ASSERT("  on list\"{}\"", breadcrumb->pCommandListDebugNameA);

                if (breadcrumb->pCommandQueueDebugNameA != nullptr)
                    PHI_LOG_ASSERT("  on queue\"{}\"", breadcrumb->pCommandQueueDebugNameA);


                auto logger = PHI_LOG_ASSERT;
                logger << "    ";

                unsigned const last_executed_i = *breadcrumb->pLastBreadcrumbValue;
                for (auto i = 0u; i < breadcrumb->BreadcrumbCount; ++i)
                {
                    if (i == last_executed_i)
                        logger << "[[-  " << breadcrumb->pCommandHistory[i] << " -]]";
                    else
                        logger << "[" << breadcrumb->pCommandHistory[i] << "]";
                }
                if (last_executed_i == breadcrumb->BreadcrumbCount)
                    logger << "  (fully executed)";
                else
                    logger << "  (last executed: " << last_executed_i << ")";

                breadcrumb = breadcrumb->pNext;
                ++num_breadcrumbs;
            }
            PHI_LOG_ASSERT << "end of breadcrumb data";
        }
        else
        {
            PHI_LOG_ASSERT << "DRED breadcrumb output query failed";
            PHI_LOG_ASSERT << "use validation_level::on_extended_dred";
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
        else
        {
            PHI_LOG_ASSERT << "DRED pagefault output query failed";
            PHI_LOG_ASSERT << "use validation_level::on_extended_dred";
        }
    }
    else
    {
        PHI_LOG_ASSERT << "no DRED active, use validation_level::on_extended_dred";
    }
}
}


void phi::d3d12::detail::verify_failure_handler(HRESULT hr, const char* expression, const char* filename, int line, ID3D12Device* device)
{
    // Make sure this really is a failed HRESULT
    CC_RUNTIME_ASSERT(FAILED(hr));

    // TODO: Proper logging
    PHI_LOG_ASSERT("backend verify on {} failed", expression);
    PHI_LOG_ASSERT("  error: {}", get_general_error_literal(hr));
    PHI_LOG_ASSERT("  file {}:{}", filename, line);

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
        PHI_LOG_ASSERT("Failed to recover device from ID3D12DeviceChild {}", device_child);
    }

    // TODO: Graceful shutdown
    std::abort();
}
