#pragma once

#include <cstdint>

namespace phi::handle
{
using index_t = int32_t;
inline constexpr index_t null_handle_index = index_t(-1);

#define PHI_DEFINE_HANDLE(_type_)                                                                         \
    struct _type_                                                                                         \
    {                                                                                                     \
        index_t index;                                                                                    \
        [[nodiscard]] constexpr bool is_valid() const noexcept { return index != null_handle_index; }     \
        [[nodiscard]] constexpr bool operator==(_type_ rhs) const noexcept { return index == rhs.index; } \
        [[nodiscard]] constexpr bool operator!=(_type_ rhs) const noexcept { return index != rhs.index; } \
    };                                                                                                    \
    inline constexpr _type_ null_##_type_ = {null_handle_index}


/// generic resource (buffer, texture, render target)
PHI_DEFINE_HANDLE(resource);

/// pipeline state (vertex layout, primitive config, shaders, framebuffer formats, ...)
PHI_DEFINE_HANDLE(pipeline_state);

/// shader_view := (SRVs + UAVs + Samplers)
/// shader argument := handle::shader_view + handle::resource (CBV) + uint (CBV offset)
PHI_DEFINE_HANDLE(shader_view);

/// recorded command list, ready to submit or discard
PHI_DEFINE_HANDLE(command_list);

/// synchronization primitive storing a uint64, can be signalled and waited on from both CPU and GPU
PHI_DEFINE_HANDLE(fence);

/// multiple contiguous queries for timestamps, occlusion or pipeline statistics
PHI_DEFINE_HANDLE(query_range);

/// raytracing acceleration structure handle
PHI_DEFINE_HANDLE(accel_struct);

#undef PHI_DEFINE_HANDLE
}
