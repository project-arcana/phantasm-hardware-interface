#pragma once

#include <clean-core/span.hh>

#include <typed-geometry/types/size.hh>

#include <phantasm-hardware-interface/common/container/flat_vector.hh>

#include "limits.hh"
#include "types.hh"

namespace phi::arg
{
struct framebuffer_config
{
    /// configs of the render targets, [0, n]
    flat_vector<render_target_config, limits::max_render_targets> render_targets;

    bool logic_op_enable = false;
    blend_logic_op logic_op = blend_logic_op::no_op;

    /// format of the depth stencil target, or format::none
    format depth_target = format::none;

public:
    void add_render_target(format fmt)
    {
        render_target_config new_rt;
        new_rt.fmt = fmt;
        render_targets.push_back(new_rt);
    }

    void set_depth_target(format fmt) { depth_target = fmt; }
    void remove_depth_target() { depth_target = format::none; }
};

struct vertex_format
{
    cc::span<vertex_attribute_info const> attributes;
    uint32_t vertex_size_bytes = 0;
};

/// A shader argument consists of SRVs, UAVs, an optional CBV, and an offset into it
struct shader_arg_shape
{
    uint32_t num_srvs = 0;
    uint32_t num_uavs = 0;
    uint32_t num_samplers = 0;
    bool has_cbv = false;

public:
    constexpr shader_arg_shape(uint32_t srvs, uint32_t uavs = 0, uint32_t samplers = 0, bool cbv = false)
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
    std::byte const* data = nullptr; ///< pointer to the (backend-dependent) shader binary data
    size_t size = 0;
};

struct graphics_shader
{
    shader_binary binary;
    shader_stage stage = shader_stage::none;
};

/// A graphics shader bundle consists of up to 1 shader per graphics stage
using graphics_shaders = cc::span<graphics_shader const>;

struct graphics_pipeline_state_desc
{
    pipeline_config config;
    framebuffer_config framebuffer;
    vertex_format vertices;

    flat_vector<graphics_shader, limits::num_graphics_shader_stages> shader_binaries;
    flat_vector<shader_arg_shape, limits::max_shader_arguments> shader_arg_shapes;
    bool has_root_constants = false;
};

struct compute_pipeline_state_desc
{
    shader_binary shader;
    flat_vector<shader_arg_shape, limits::max_shader_arguments> shader_arg_shapes;
    bool has_root_constants = false;
};

/// an element in a bottom-level acceleration strucutre
struct blas_element
{
    /// the vertex buffer containing positions
    buffer_address vertex_addr;
    /// amount of vertices to use
    uint32_t num_vertices = 0;
    /// the vertex position format
    /// positions must come first in the vertex struct
    format vertex_pos_format = format::rgb32f;

    /// the index buffer to use, optional
    buffer_address index_addr;
    /// amount of indices to read
    uint32_t num_indices;

    /// location in a buffer containing a 3x4 affine transform matrix (row major), optional
    buffer_address transform_addr;

    /// if true, the geometry acts as if no any-hit shader is present when hit
    /// enable wherever possible (can be overriden using flags in TraceRay)
    bool is_opaque = true;
};

struct raytracing_library_export
{
    shader_stage stage = shader_stage::none;
    char const* entrypoint = nullptr;
};

/// a shader library lists the symbol names it exports
struct raytracing_shader_library
{
    shader_binary binary;
    flat_vector<raytracing_library_export, 16> shader_exports;
};

/// associates exports from libraries with their argument shapes
struct raytracing_argument_association
{
    enum e_arg_association_target : uint8_t
    {
        e_target_identifiable_shader,
        e_target_hitgroup
    };

    /// an argument association targets an identifiable shader (identfiable: ray_gen, ray_miss or ray_callable), or a hitgroup
    e_arg_association_target target_type = e_target_identifiable_shader;
    /// order corresponds to the order of exports/hitgroups at PSO creation
    /// NOTE: identifiable shaders are indexed contiguously across libraries, and non-identifiable shaders are skipped
    flat_vector<uint32_t, 16> target_indices;

    flat_vector<shader_arg_shape, limits::max_shader_arguments> argument_shapes;
    bool has_root_constants = false;

public:
    void set_target_identifiable() { target_type = e_target_identifiable_shader; }
    void set_target_hitgroup() { target_type = e_target_hitgroup; }

    void add_shader_arg(uint32_t num_srvs, uint32_t num_uavs, uint32_t num_samplers, bool has_cbv)
    {
        argument_shapes.push_back(shader_arg_shape{num_srvs, num_uavs, num_samplers, has_cbv});
    }
};

/// a triangle hit group, has a closest hit shader, and optionally an any hit and intersection shader
struct raytracing_hit_group
{
    char const* name = nullptr;

    /// order corresponds to the order of exports, flat across all libraries
    int closest_hit_export_index = -1;
    int any_hit_export_index = -1;      ///< optional
    int intersection_export_index = -1; ///< optional
};

struct raytracing_pipeline_state_desc
{
    cc::span<raytracing_shader_library const> libraries;
    cc::span<raytracing_argument_association const> argument_associations;
    cc::span<raytracing_hit_group const> hit_groups;

    uint32_t max_recursion = 0;
    uint32_t max_payload_size_bytes = 0;
    uint32_t max_attribute_size_bytes = 0;
};

struct shader_table_record
{
    enum e_table_record_target : uint8_t
    {
        e_target_identifiable_shader,
        e_target_hitgroup
    };

    /// a shader table record targets an identifiable shader (identfiable: ray_gen, ray_miss or ray_callable), or a hitgroup
    e_table_record_target target_type = e_target_identifiable_shader;
    /// order corresponds to the order of exports/hitgroups at PSO creation
    /// NOTE: identifiable shaders are indexed contiguously across libraries, and non-identifiable shaders are skipped
    uint32_t target_index = 0;

    void const* root_arg_data = nullptr; ///< optional, data of the root constant data
    uint32_t root_arg_size_bytes = 0;    ///< size of the root constant data
    flat_vector<shader_argument, limits::max_shader_arguments> shader_arguments;

    void set_shader(uint32_t index)
    {
        target_type = e_target_identifiable_shader;
        target_index = index;
    }

    void set_hitgroup(uint32_t index)
    {
        target_type = e_target_hitgroup;
        target_index = index;
    }

    void add_shader_arg(handle::resource cbv, uint32_t cbv_off = 0, handle::shader_view sv = handle::null_shader_view)
    {
        shader_arguments.push_back(shader_argument{cbv, sv, cbv_off});
    }
};

// resource creation info

struct create_render_target_info
{
    phi::format format;
    int width;
    int height;
    uint32_t num_samples;
    uint32_t array_size;
    rt_clear_value clear_value;

public:
    static create_render_target_info create(
        phi::format fmt, tg::isize2 size, uint32_t num_samples = 1, uint32_t array_size = 1, rt_clear_value clear_val = {0.f, 0.f, 0.f, 1.f})
    {
        return create_render_target_info{fmt, size.width, size.height, num_samples, array_size, clear_val};
    }
    constexpr bool operator==(create_render_target_info const& rhs) const noexcept
    {
        return format == rhs.format && width == rhs.width && height == rhs.height && num_samples == rhs.num_samples && array_size == rhs.array_size;
    }
};

struct create_texture_info
{
    phi::format fmt;
    phi::texture_dimension dim;
    bool allow_uav;
    int width;
    int height;
    uint32_t depth_or_array_size;
    uint32_t num_mips;

public:
    static create_texture_info create(phi::format fmt,
                                      tg::isize2 size,
                                      uint32_t num_mips = 1,
                                      phi::texture_dimension dim = phi::texture_dimension::t2d,
                                      uint32_t depth_or_array_size = 1,
                                      bool allow_uav = false)
    {
        return create_texture_info{fmt, dim, allow_uav, size.width, size.height, depth_or_array_size, num_mips};
    }

    constexpr bool operator==(create_texture_info const& rhs) const noexcept
    {
        return fmt == rhs.fmt && dim == rhs.dim && allow_uav == rhs.allow_uav && width == rhs.width && height == rhs.height
               && depth_or_array_size == rhs.depth_or_array_size && num_mips == rhs.num_mips;
    }
};

struct create_buffer_info
{
    uint32_t size_bytes;
    uint32_t stride_bytes;
    bool allow_uav;
    phi::resource_heap heap;

public:
    static create_buffer_info create(uint32_t size_bytes, uint32_t stride_bytes, phi::resource_heap heap = phi::resource_heap::gpu, bool allow_uav = false)
    {
        return create_buffer_info{size_bytes, stride_bytes, allow_uav, heap};
    }
    constexpr bool operator==(create_buffer_info const& rhs) const noexcept
    {
        return size_bytes == rhs.size_bytes && stride_bytes == rhs.stride_bytes && allow_uav == rhs.allow_uav && heap == rhs.heap;
    }
};

struct create_resource_info
{
    enum e_resource_type
    {
        e_resource_undefined,
        e_resource_render_target,
        e_resource_texture,
        e_resource_buffer
    };

    e_resource_type type = e_resource_undefined;

    union {
        create_render_target_info info_render_target;
        create_texture_info info_texture;
        create_buffer_info info_buffer;
    };

public:
    // static convenience

    static create_resource_info create(create_render_target_info const& rt_info)
    {
        create_resource_info res;
        res.type = e_resource_render_target;
        res.info_render_target = rt_info;
        return res;
    }
    static create_resource_info create(create_texture_info const& tex_info)
    {
        create_resource_info res;
        res.type = e_resource_texture;
        res.info_texture = tex_info;
        return res;
    }
    static create_resource_info create(create_buffer_info const& buf_info)
    {
        create_resource_info res;
        res.type = e_resource_buffer;
        res.info_buffer = buf_info;
        return res;
    }

    static create_resource_info render_target(
        phi::format fmt, tg::isize2 size, uint32_t num_samples = 1, uint32_t array_size = 1, rt_clear_value clear_val = {0.f, 0.f, 0.f, 1.f})
    {
        return create(create_render_target_info::create(fmt, size, num_samples, array_size, clear_val));
    }

    static create_resource_info texture(phi::format fmt,
                                        tg::isize2 size,
                                        uint32_t num_mips = 1,
                                        phi::texture_dimension dim = phi::texture_dimension::t2d,
                                        uint32_t depth_or_array_size = 1,
                                        bool allow_uav = false)
    {
        return create(create_texture_info::create(fmt, size, num_mips, dim, depth_or_array_size, allow_uav));
    }

    static create_resource_info buffer(uint32_t size_bytes, uint32_t stride_bytes, phi::resource_heap heap = phi::resource_heap::gpu, bool allow_uav = false)
    {
        return create(create_buffer_info::create(size_bytes, stride_bytes, heap, allow_uav));
    }
};
}
