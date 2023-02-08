#include "diagnostic_util.hh"

// clang-format off
#include "d3d12_sanitized.hh"
#ifdef PHI_HAS_PIX
#include <DXProgrammableCapture.h>
#include <WinPixEventRuntime/pix3.h>
#endif
// clang-format on

#include <renderdoc_app/renderdoc_app.h>

#include <phantasm-hardware-interface/features/renderdoc_loader.hh>
#include <phantasm-hardware-interface/common/log.hh>

#include "verify.hh"

void phi::d3d12::util::diagnostic_state::init()
{
#ifdef PHI_HAS_PIX
    // PIX
    if (detail::hr_succeeded(::DXGIGetDebugInterface1(0, IID_PPV_ARGS(&_pix_handle))))
    {
        _pix_capture_running = false;
		// This succeeds if PIX is attached, but also if Renderdoc is, possibly others
        //PHI_LOG << "PIX detected";
    }
    else
#endif
    {
        _pix_handle = nullptr;
    }

    // RenderDoc
    _renderdoc_handle = ::phi::detail::load_renderdoc();
}

void phi::d3d12::util::diagnostic_state::free()
{
    end_capture();

#ifdef PHI_HAS_PIX
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

bool phi::d3d12::util::diagnostic_state::start_capture()
{
#ifdef PHI_HAS_PIX
    if (_pix_handle)
    {
        PHI_LOG << "starting PIX capture";
        _pix_handle->BeginCapture();
        _pix_capture_running = true;
        return true;
    }
#endif

    if (_renderdoc_handle)
    {
        PHI_LOG << "starting RenderDoc capture";
        _renderdoc_handle->StartFrameCapture(nullptr, nullptr);
        _renderdoc_capture_running = true;
        return true;
    }

    return false;
}

bool phi::d3d12::util::diagnostic_state::end_capture()
{
#ifdef PHI_HAS_PIX
    if (_pix_handle && _pix_capture_running)
    {
        PHI_LOG << "ending PIX capture";
        _pix_handle->EndCapture();
        _pix_capture_running = false;
        return true;
    }
#endif

    if (_renderdoc_handle && _renderdoc_capture_running)
    {
        PHI_LOG << "ending RenderDoc capture";
        _renderdoc_handle->EndFrameCapture(nullptr, nullptr);
        _renderdoc_capture_running = false;
        return true;
    }

    return false;
}

void phi::d3d12::util::begin_pix_marker(ID3D12GraphicsCommandList* cmdlist, UINT64 color, const char* string)
{
#ifdef PHI_HAS_PIX
    ::PIXBeginEvent(cmdlist, color, string);
#else
    (void)cmdlist;
    (void)color;
    (void)string;
    PHI_LOG_WARN("PIX integration missing, enable the PHI_ENABLE_D3D12_PIX CMake option");
#endif
}

void phi::d3d12::util::end_pix_marker(ID3D12GraphicsCommandList* cmdlist)
{
#ifdef PHI_HAS_PIX
    ::PIXEndEvent(cmdlist);
#else
    (void)cmdlist;
    PHI_LOG_WARN("PIX integration missing, enable the PHI_ENABLE_D3D12_PIX CMake option");
#endif
}
