#pragma once

#include <cstdint>

namespace phi::handle
{
using index_t = uint32_t;
inline constexpr index_t null_handle_value = index_t(-1);

namespace detail
{
struct abstract_handle
{
    index_t _value;
    abstract_handle() = default;
    constexpr abstract_handle(index_t val) : _value(val) {}
    [[nodiscard]] constexpr bool is_valid() const noexcept { return _value != null_handle_value; }
    [[nodiscard]] constexpr bool operator==(abstract_handle rhs) const noexcept { return _value == rhs._value; }
    [[nodiscard]] constexpr bool operator!=(abstract_handle rhs) const noexcept { return _value != rhs._value; }
};
}

#define PHI_DEFINE_HANDLE(_type_)                                 \
    struct _type_ : public ::phi::handle::detail::abstract_handle \
    {                                                             \
    };                                                            \
    inline constexpr _type_ null_##_type_ = {::phi::handle::null_handle_value}


/// generic resource (buffer, texture, render target)
PHI_DEFINE_HANDLE(resource);

/// pipeline state (vertex layout, primitive config, shaders, framebuffer formats, ...)
PHI_DEFINE_HANDLE(pipeline_state);

/// shader_view := (SRVs + UAVs + Samplers)
/// shader argument := handle::shader_view + handle::resource (CBV) + uint (CBV offset)
PHI_DEFINE_HANDLE(shader_view);

/// recorded command list, ready to submit or discard
PHI_DEFINE_HANDLE(command_list);

/// swapchain on a window
PHI_DEFINE_HANDLE(swapchain);

/// synchronization primitive storing a uint64, can be signalled and waited on from both CPU and GPU
PHI_DEFINE_HANDLE(fence);

/// multiple contiguous queries for timestamps, occlusion or pipeline statistics
PHI_DEFINE_HANDLE(query_range);

/// raytracing acceleration structure handle
PHI_DEFINE_HANDLE(accel_struct);
}
