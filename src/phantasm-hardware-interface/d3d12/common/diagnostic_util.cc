#include "diagnostic_util.hh"

// clang-format off
#include "d3d12_sanitized.hh"
#ifdef PR_BACKEND_D3D12_HAS_PIX
#include <DXProgrammableCapture.h>
#include <WinPixEventRuntime/pix3.h>
#endif
// clang-format on

#include <renderdoc_app/renderdoc_app.h>

#include <phantasm-hardware-interface/detail/renderdoc_loader.hh>
#include <phantasm-hardware-interface/d3d12/common/log.hh>

#include "verify.hh"

void pr::backend::d3d12::util::diagnostic_state::init()
{
#ifdef PR_BACKEND_D3D12_HAS_PIX
    // PIX
    if (detail::hr_succeeded(::DXGIGetDebugInterface1(0, IID_PPV_ARGS(&_pix_handle))))
    {
        _pix_capture_running = false;
        log::info() << "PIX detected";
    }
    else
#endif
    {
        _pix_handle = nullptr;
    }

    // RenderDoc
    _renderdoc_handle = backend::detail::load_renderdoc();
    if (_renderdoc_handle)
    {
        log::info() << "RenderDoc detected";
    }
}

void pr::backend::d3d12::util::diagnostic_state::free()
{
    end_capture();

#ifdef PR_BACKEND_D3D12_HAS_PIX
    if (_pix_handle)
    {
        _pix_handle->Release();
        _pix_handle = nullptr;
    }
#endif

    if (_renderdoc_handle)
    {
        // anything to do here?
        _renderdoc_handle = nullptr;
    }
}

bool pr::backend::d3d12::util::diagnostic_state::start_capture()
{
#ifdef PR_BACKEND_D3D12_HAS_PIX
    if (_pix_handle)
    {
        log::info() << "starting PIX capture";
        _pix_handle->BeginCapture();
        _pix_capture_running = true;
        return true;
    }
#endif

    if (_renderdoc_handle)
    {
        log::info() << "starting RenderDoc capture";
        _renderdoc_handle->StartFrameCapture(nullptr, nullptr);
        _renderdoc_capture_running = true;
        return true;
    }

    return false;
}

bool pr::backend::d3d12::util::diagnostic_state::end_capture()
{
#ifdef PR_BACKEND_D3D12_HAS_PIX
    if (_pix_handle && _pix_capture_running)
    {
        log::info() << "ending PIX capture";
        _pix_handle->EndCapture();
        _pix_capture_running = false;
        return true;
    }
#endif

    if (_renderdoc_handle && _renderdoc_capture_running)
    {
        log::info() << "ending RenderDoc capture";
        _renderdoc_handle->EndFrameCapture(nullptr, nullptr);
        _renderdoc_capture_running = false;
        return true;
    }

    return false;
}

void pr::backend::d3d12::util::set_pix_marker(ID3D12GraphicsCommandList* cmdlist, UINT64 color, const char* string)
{
#ifdef PR_BACKEND_D3D12_HAS_PIX
    ::PIXSetMarker(cmdlist, color, string);
#else
    (void)cmdlist;
    (void)color;
    (void)string;
    log::err()("PIX integration missing, enable the PR_ENABLE_D3D12_PIX CMake option");
#endif
}

void pr::backend::d3d12::util::set_pix_marker(ID3D12CommandQueue* cmdqueue, UINT64 color, const char* string)
{
#ifdef PR_BACKEND_D3D12_HAS_PIX
    ::PIXSetMarker(cmdqueue, color, string);
#else
    (void)cmdqueue;
    (void)color;
    (void)string;
    log::err()("PIX integration missing, enable the PR_ENABLE_D3D12_PIX CMake option");
#endif
}

void pr::backend::d3d12::util::set_pix_marker_cpu(UINT64 color, const char* string)
{
#ifdef PR_BACKEND_D3D12_HAS_PIX
    ::PIXSetMarker(color, string);
#else
    (void)color;
    (void)string;
    log::err()("PIX integration missing, enable the PR_ENABLE_D3D12_PIX CMake option");
#endif
}
