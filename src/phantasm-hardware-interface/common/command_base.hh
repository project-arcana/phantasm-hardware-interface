#pragma once

#include <cstdint>

namespace phi::cmd::detail
{
#define PHI_CMD_TYPE_VALUES        \
    PHI_X(draw)                    \
    PHI_X(draw_indirect)           \
    PHI_X(dispatch)                \
    PHI_X(transition_resources)    \
    PHI_X(barrier_uav)             \
    PHI_X(transition_image_slices) \
    PHI_X(copy_buffer)             \
    PHI_X(copy_texture)            \
    PHI_X(copy_buffer_to_texture)  \
    PHI_X(copy_texture_to_buffer)  \
    PHI_X(resolve_texture)         \
    PHI_X(begin_render_pass)       \
    PHI_X(end_render_pass)         \
    PHI_X(write_timestamp)         \
    PHI_X(resolve_queries)         \
    PHI_X(begin_debug_label)       \
    PHI_X(end_debug_label)         \
    PHI_X(update_bottom_level)     \
    PHI_X(update_top_level)        \
    PHI_X(dispatch_rays)           \
    PHI_X(clear_textures)          \
    PHI_X(code_location_marker)    \
    PHI_X(begin_profile_scope)     \
    PHI_X(end_profile_scope)

enum class cmd_type : uint8_t
{
#define PHI_X(_val_) _val_,
    PHI_CMD_TYPE_VALUES
#undef PHI_X
};

struct cmd_base
{
    cmd_type s_internal_type;
    cmd_base(cmd_type t) : s_internal_type(t) {}
};

template <cmd_type TYPE>
struct typed_cmd : cmd_base
{
    typed_cmd() : cmd_base(TYPE) {}
};
}

#define PHI_DEFINE_CMD(_type_) struct _type_ final : detail::typed_cmd<detail::cmd_type::_type_>
