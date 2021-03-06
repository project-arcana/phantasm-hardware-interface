#include "diagnostic_util.hh"

#include <renderdoc_app/renderdoc_app.h>

#include <phantasm-hardware-interface/common/log.hh>
#include <phantasm-hardware-interface/features/renderdoc_loader.hh>

void phi::vk::util::diagnostic_state::init()
{
    // RenderDoc
    _renderdoc_handle = phi::detail::load_renderdoc();
    if (_renderdoc_handle)
    {
        PHI_LOG("RenderDoc detected");
    }
}

void phi::vk::util::diagnostic_state::free()
{
    end_capture();

    if (_renderdoc_handle)
    {
        // anything to do here?
        _renderdoc_handle = nullptr;
    }
}

bool phi::vk::util::diagnostic_state::start_capture()
{
    if (_renderdoc_handle)
    {
        PHI_LOG("starting RenderDoc capture");
        _renderdoc_handle->StartFrameCapture(nullptr, nullptr);
        _renderdoc_capture_running = true;
        return true;
    }

    return false;
}

bool phi::vk::util::diagnostic_state::end_capture()
{
    if (_renderdoc_handle && _renderdoc_capture_running)
    {
        PHI_LOG("ending RenderDoc capture");
        _renderdoc_handle->EndFrameCapture(nullptr, nullptr);
        _renderdoc_capture_running = false;
        return true;
    }

    return false;
}
