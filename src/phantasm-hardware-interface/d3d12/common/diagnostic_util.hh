#pragma once

#include "d3d12_fwd.hh"

typedef struct RENDERDOC_API_1_4_0 RENDERDOC_API_1_4_0;

namespace phi::d3d12::util
{
struct diagnostic_state
{
    void init();
    void free();

    bool start_capture();
    bool end_capture();

private:
    IDXGraphicsAnalysis* _pix_handle = nullptr;
    RENDERDOC_API_1_4_0* _renderdoc_handle = nullptr;

    bool _pix_capture_running = false;
    bool _renderdoc_capture_running = false;
};

void begin_pix_marker(ID3D12GraphicsCommandList* cmdlist, UINT64 color, char const* string);
void end_pix_marker(ID3D12GraphicsCommandList* cmdlist);

}
