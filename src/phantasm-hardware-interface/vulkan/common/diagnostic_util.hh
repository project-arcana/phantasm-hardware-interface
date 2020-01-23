#pragma once

typedef struct RENDERDOC_API_1_4_0 RENDERDOC_API_1_4_0;

namespace phi::vk::util
{
struct diagnostic_state
{
    void init();
    void free();

    bool start_capture();
    bool end_capture();

    bool is_renderdoc_present() const { return _renderdoc_handle != nullptr; }

private:
    RENDERDOC_API_1_4_0* _renderdoc_handle = nullptr;

    bool _renderdoc_capture_running = false;
};
}
