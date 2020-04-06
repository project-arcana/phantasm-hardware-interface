#pragma once

#include <phantasm-hardware-interface/arguments.hh>

// namespace phi::debug
//{
#define PHI_INS(_field_) i(v._field_, #_field_)

template <class I>
constexpr void introspect(I&& i, phi::vertex_attribute_info const& v)
{
    PHI_INS(semantic_name);
    PHI_INS(offset);
}

template <class I>
constexpr void introspect(I&& i, phi::arg::vertex_format const& v)
{
    for (auto const& val : v.attributes)
        i(val, "attributes[]");
    PHI_INS(vertex_size_bytes);
}

template <class I>
constexpr void introspect(I&& i, phi::render_target_config const& v)
{
    PHI_INS(fmt);
    PHI_INS(blend_enable);
    PHI_INS(blend_color_src);
    PHI_INS(blend_color_dest);
    PHI_INS(blend_op_color);
    PHI_INS(blend_alpha_src);
    PHI_INS(blend_alpha_dest);
    PHI_INS(blend_op_alpha);
}

template <class I>
constexpr void introspect(I&& i, phi::arg::framebuffer_config const& v)
{
    PHI_INS(render_targets);
    PHI_INS(logic_op_enable);
    PHI_INS(logic_op);
    PHI_INS(depth_target);
}

template <class I>
constexpr void introspect(I&& i, phi::arg::shader_arg_shape const& v)
{
    PHI_INS(num_srvs);
    PHI_INS(num_uavs);
    PHI_INS(num_samplers);
    PHI_INS(has_cbv);
}

#undef PHI_INS
//}
