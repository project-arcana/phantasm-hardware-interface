#pragma once

#include <clean-core/capped_vector.hh>
#include <clean-core/span.hh>

#include "limits.hh"
#include "types.hh"

namespace phi::arg
{
struct framebuffer_config
{
    /// configs of the render targets, [0, n]
    cc::capped_vector<render_target_config, limits::max_render_targets> render_targets;

    bool logic_op_enable = false;
    blend_logic_op logic_op = blend_logic_op::no_op;

    /// format of the depth stencil target, [0, 1]
    cc::capped_vector<format, 1> depth_target;

    void add_render_target(format fmt)
    {
        render_target_config new_rt;
        new_rt.format = fmt;
        render_targets.push_back(new_rt);
    }
};

struct vertex_format
{
    cc::span<vertex_attribute_info const> attributes;
    unsigned vertex_size_bytes;
};

/// A shader argument consists of SRVs, UAVs, an optional CBV, and an offset into it
struct shader_arg_shape
{
    unsigned num_srvs = 0;
    unsigned num_uavs = 0;
    unsigned num_samplers = 0;
    bool has_cbv = false;

    shader_arg_shape(unsigned srvs, unsigned uavs = 0, unsigned samplers = 0, bool cbv = false)
      : num_srvs(srvs), num_uavs(uavs), num_samplers(samplers), has_cbv(cbv)
    {
    }
    shader_arg_shape() = default;

    constexpr bool operator==(shader_arg_shape const& rhs) const noexcept
    {
        return num_srvs == rhs.num_srvs && num_uavs == rhs.num_uavs && has_cbv == rhs.has_cbv && num_samplers == rhs.num_samplers;
    }
};

/// A shader payload consists of [1, 4] shader arguments
using shader_arg_shapes = cc::span<shader_arg_shape const>;

struct shader_binary
{
    std::byte* data; ///< pointer to the (backend-dependent) shader binary data
    size_t size;
};

struct graphics_shader
{
    shader_binary binary;
    shader_stage stage;
};

/// A graphics shader bundle consists of up to 1 shader per graphics stage
using graphics_shaders = cc::span<graphics_shader const>;

inline bool operator==(shader_arg_shapes const& lhs, shader_arg_shapes const& rhs) noexcept
{
    if (lhs.size() != rhs.size())
        return false;

    for (auto i = 0u; i < lhs.size(); ++i)
    {
        if (!(lhs[i] == rhs[i]))
            return false;
    }

    return true;
}

/// an element in a bottom-level acceleration strucutre
struct blas_element
{
    handle::resource vertex_buffer = handle::null_resource;
    handle::resource index_buffer = handle::null_resource; ///< optional
    unsigned num_vertices = 0;
    unsigned num_indices = 0;
    bool is_opaque = true;
};

/// a raytracing shader library lists the symbol names it exports
struct rt_shader_library
{
    shader_binary binary;
    cc::capped_vector<wchar_t const*, 16> symbols;
};

/// associates symbols exported from libraries with their argument shapes
struct rt_argument_association
{
    cc::capped_vector<wchar_t const*, 16> symbols;
    cc::capped_vector<shader_arg_shape, limits::max_shader_arguments> argument_shapes;
    bool has_root_constants = false;
};

struct rt_hit_group
{
    wchar_t const* name = nullptr;
    wchar_t const* closest_hit_symbol = nullptr;
    wchar_t const* any_hit_symbol = nullptr;      ///< optional
    wchar_t const* intersection_symbol = nullptr; ///< optional
};

struct shader_table_record
{
    wchar_t const* symbol = nullptr;     ///< name of the shader or hit group
    void const* root_arg_data = nullptr; ///< optional, data of the root constant data
    uint32_t root_arg_size = 0;          ///< size of the root constant data
    cc::capped_vector<shader_arg, limits::max_shader_arguments> shader_arguments;
};

using rt_shader_libraries = cc::span<rt_shader_library const>;
using rt_argument_associations = cc::span<rt_argument_association const>;
using rt_hit_groups = cc::span<rt_hit_group const>;
using shader_table_records = cc::span<shader_table_record const>;
}
