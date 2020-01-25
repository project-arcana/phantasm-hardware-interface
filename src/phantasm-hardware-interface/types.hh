#pragma once

#include <cstdint>

#include <clean-core/flags.hh>

namespace phi
{
namespace handle
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

/// synchronization primitive. can be "set" by a command_list after it completed executing
PHI_DEFINE_HANDLE(event);

/// raytracing acceleration structure handle
PHI_DEFINE_HANDLE(accel_struct);

#undef PHI_DEFINE_HANDLE
}

struct shader_arg
{
    handle::resource constant_buffer;
    handle::shader_view shader_view;
    unsigned constant_buffer_offset;
};

enum class shader_domain : uint8_t
{
    // graphics
    vertex,
    hull,
    domain,
    geometry,
    pixel,

    // compute
    compute,

    // raytracing
    ray_gen,
    ray_miss,
    ray_closest_hit,
    ray_intersect,
    ray_any_hit,
};

using shader_domain_flags_t = cc::flags<shader_domain, 16>;
CC_FLAGS_ENUM_SIZED(shader_domain, 16);

inline constexpr shader_domain_flags_t shader_domain_mask_all_graphics
    = shader_domain::vertex | shader_domain::hull | shader_domain::domain | shader_domain::geometry | shader_domain::pixel;

inline constexpr shader_domain_flags_t shader_domain_mask_all_ray
    = shader_domain::ray_gen | shader_domain::ray_miss | shader_domain::ray_closest_hit | shader_domain::ray_intersect | shader_domain::ray_any_hit;

enum class queue_type : uint8_t
{
    graphics,
    copy,
    compute
};

// Maps to
// D3D12: resource states
// Vulkan: access masks, image layouts and pipeline stage dependencies
enum class resource_state : uint8_t
{
    // unknown to pr
    unknown,
    // undefined in API semantics
    undefined,

    vertex_buffer,
    index_buffer,

    constant_buffer,
    shader_resource,
    unordered_access,

    render_target,
    depth_read,
    depth_write,

    indirect_argument,

    copy_src,
    copy_dest,

    resolve_src,
    resolve_dest,

    present,

    raytrace_accel_struct,
};

// Maps to DXGI_FORMAT and VkFormat
// [f]loat, [i]nt, [u]int, [un]orm
enum class format : uint8_t
{
    rgba32f,
    rgb32f,
    rg32f,
    r32f,
    rgba32i,
    rgb32i,
    rg32i,
    r32i,
    rgba32u,
    rgb32u,
    rg32u,
    r32u,
    rgba16i,
    rg16i,
    r16i,
    rgba16u,
    rg16u,
    r16u,
    rgba16f,
    rg16f,
    r16f,
    rgba8i,
    rg8i,
    r8i,
    rgba8u,
    rg8u,
    r8u,
    rgba8un,
    rg8un,
    r8un,

    // backbuffer formats
    bgra8un,

    // depth formats
    depth32f,
    depth16un,

    // depth stencil formats
    depth32f_stencil8u,
    depth24un_stencil8u,
};

/// returns true if the format is a depth OR depth stencil format
[[nodiscard]] inline constexpr bool is_depth_format(format fmt) { return fmt >= format::depth32f; }

/// returns true if the format is a depth stencil format
[[nodiscard]] inline constexpr bool is_depth_stencil_format(format fmt) { return fmt >= format::depth32f_stencil8u; }

/// information about a single vertex attribute
struct vertex_attribute_info
{
    char const* semantic_name;
    unsigned offset;
    format format;
};

enum class texture_dimension : uint8_t
{
    t1d,
    t2d,
    t3d
};

enum class shader_view_dimension : uint8_t
{
    buffer,
    texture1d,
    texture1d_array,
    texture2d,
    texture2d_ms,
    texture2d_array,
    texture2d_ms_array,
    texture3d,
    texturecube,
    texturecube_array,
    raytracing_accel_struct
};

struct shader_view_elem
{
    handle::resource resource;

    format pixel_format;
    shader_view_dimension dimension;

    struct sve_texture_info
    {
        unsigned mip_start;   ///< index of the first usable mipmap (usually: 0)
        unsigned mip_size;    ///< amount of usable mipmaps, starting from mip_start (usually: -1 / all)
        unsigned array_start; ///< index of the first usable array element [if applicable] (usually: 0)
        unsigned array_size;  ///< amount of usable array elements [if applicable]
    };

    struct sve_buffer_info
    {
        unsigned element_start;        ///< index of the first element in the buffer
        unsigned num_elements;         ///< amount of elements in the buffer
        unsigned element_stride_bytes; ///< the stride of elements in bytes
    };

    union {
        sve_texture_info texture_info;
        sve_buffer_info buffer_info;
    };

public:
    // convenience

    void init_as_null() { resource = handle::null_resource; }

    void init_as_backbuffer(handle::resource res)
    {
        resource = res;
        // cmdlist translation checks for this case and automatically chooses the right
        // format, no need to specify anything else
    }

    void init_as_tex2d(handle::resource res, format pf, bool multisampled = false, unsigned mip_start = 0, unsigned mip_size = unsigned(-1))
    {
        resource = res;
        pixel_format = pf;
        dimension = multisampled ? shader_view_dimension::texture2d_ms : shader_view_dimension::texture2d;
        texture_info.mip_start = mip_start;
        texture_info.mip_size = mip_size;
        texture_info.array_start = 0;
        texture_info.array_size = 1;
    }

    void init_as_texcube(handle::resource res, format pf)
    {
        resource = res;
        pixel_format = pf;
        dimension = shader_view_dimension::texturecube;
        texture_info.mip_start = 0;
        texture_info.mip_size = unsigned(-1);
        texture_info.array_start = 0;
        texture_info.array_size = 1;
    }

    void init_as_structured_buffer(handle::resource res, unsigned num_elements, unsigned stride_bytes)
    {
        resource = res;
        dimension = shader_view_dimension::buffer;
        buffer_info.num_elements = num_elements;
        buffer_info.element_start = 0;
        buffer_info.element_stride_bytes = stride_bytes;
    }

    /// receive the buffer handle from getAccelStructBuffer
    void init_as_accel_struct(handle::resource as_buffer)
    {
        resource = as_buffer;
        dimension = shader_view_dimension::raytracing_accel_struct;
    }

public:
    // static convenience

    static shader_view_elem null()
    {
        shader_view_elem rv;
        rv.init_as_null();
        return rv;
    }
    static shader_view_elem backbuffer(handle::resource res)
    {
        shader_view_elem rv;
        rv.init_as_backbuffer(res);
        return rv;
    }
    static shader_view_elem tex2d(handle::resource res, format pf, bool multisampled = false, unsigned mip_start = 0, unsigned mip_size = unsigned(-1))
    {
        shader_view_elem rv;
        rv.init_as_tex2d(res, pf, multisampled, mip_start, mip_size);
        return rv;
    }
    static shader_view_elem texcube(handle::resource res, format pf)
    {
        shader_view_elem rv;
        rv.init_as_texcube(res, pf);
        return rv;
    }
    static shader_view_elem structured_buffer(handle::resource res, unsigned num_elements, unsigned stride_bytes)
    {
        shader_view_elem rv;
        rv.init_as_structured_buffer(res, num_elements, stride_bytes);
        return rv;
    }
    static shader_view_elem accel_struct(handle::resource as_buffer)
    {
        shader_view_elem rv;
        rv.init_as_accel_struct(as_buffer);
        return rv;
    }
};

enum class sampler_filter : uint8_t
{
    min_mag_mip_point,
    min_point_mag_linear_mip_point,
    min_linear_mag_mip_point,
    min_mag_linear_mip_point,
    min_point_mag_mip_linear,
    min_linear_mag_point_mip_linear,
    min_mag_point_mip_linear,
    min_mag_mip_linear,
    anisotropic
};

enum class sampler_address_mode : uint8_t
{
    wrap,
    clamp,
    clamp_border,
    mirror
};

enum class sampler_compare_func : uint8_t
{
    never,
    less,
    equal,
    less_equal,
    greater,
    not_equal,
    greater_equal,
    always,

    disabled
};

enum class sampler_border_color : uint8_t
{
    black_transparent_float,
    black_transparent_int,
    black_float,
    black_int,
    white_float,
    white_int
};

struct sampler_config
{
    sampler_filter filter;
    sampler_address_mode address_u;
    sampler_address_mode address_v;
    sampler_address_mode address_w;
    float min_lod;
    float max_lod;
    float lod_bias;          ///< offset from the calculated MIP level (sampled = calculated + lod_bias)
    unsigned max_anisotropy; ///< maximum amount of anisotropy in [1, 16], req. sampler_filter::anisotropic
    sampler_compare_func compare_func;
    sampler_border_color border_color; ///< the border color to use, req. sampler_filter::clamp_border

    void init_default(sampler_filter filter, unsigned anisotropy = 16u)
    {
        this->filter = filter;
        address_u = sampler_address_mode::wrap;
        address_v = sampler_address_mode::wrap;
        address_w = sampler_address_mode::wrap;
        min_lod = 0.f;
        max_lod = 100000.f;
        lod_bias = 0.f;
        max_anisotropy = anisotropy;
        compare_func = sampler_compare_func::disabled;
        border_color = sampler_border_color::white_float;
    }

    sampler_config(sampler_filter filter, unsigned anisotropy = 16u) { init_default(filter, anisotropy); }
    sampler_config() = default;
};

inline constexpr bool operator==(sampler_config const& lhs, sampler_config const& rhs) noexcept
{
    return lhs.filter == rhs.filter &&                 //
           lhs.address_u == rhs.address_u &&           //
           lhs.address_v == rhs.address_v &&           //
           lhs.address_w == rhs.address_w &&           //
           lhs.min_lod == rhs.min_lod &&               //
           lhs.max_lod == rhs.max_lod &&               //
           lhs.lod_bias == rhs.lod_bias &&             //
           lhs.max_anisotropy == rhs.max_anisotropy && //
           lhs.compare_func == rhs.compare_func &&     //
           lhs.border_color == rhs.border_color;       //
}

enum class primitive_topology : uint8_t
{
    triangles,
    lines,
    points,
    patches
};

enum class depth_function : uint8_t
{
    none,
    less,
    less_equal,
    greater,
    greater_equal,
    equal,
    not_equal,
    always,
    never
};

enum class cull_mode : uint8_t
{
    none,
    back,
    front
};

struct graphics_pipeline_config
{
    primitive_topology topology = primitive_topology::triangles;
    depth_function depth = depth_function::less;
    bool depth_readonly = false;
    cull_mode cull = cull_mode::back;
    int samples = 1;
};

enum class rt_clear_type : uint8_t
{
    clear,
    dont_care,
    load
};

enum class blend_logic_op : uint8_t
{
    no_op,
    op_clear,
    op_set,
    op_copy,
    op_copy_inverted,
    op_invert,
    op_and,
    op_nand,
    op_and_inverted,
    op_and_reverse,
    op_or,
    op_nor,
    op_xor,
    op_or_reverse,
    op_or_inverted,
    op_equiv
};

enum class blend_op : uint8_t
{
    op_add,
    op_subtract,
    op_reverse_subtract,
    op_min,
    op_max
};

enum class blend_factor : uint8_t
{
    zero,
    one,
    src_color,
    inv_src_color,
    src_alpha,
    inv_src_alpha,
    dest_color,
    inv_dest_color,
    dest_alpha,
    inv_dest_alpha
};

struct render_target_config
{
    format format = format::rgba8un;
    bool blend_enable = false;
    blend_factor blend_color_src = blend_factor::one;
    blend_factor blend_color_dest = blend_factor::zero;
    blend_op blend_op_color = blend_op::op_add;
    blend_factor blend_alpha_src = blend_factor::one;
    blend_factor blend_alpha_dest = blend_factor::zero;
    blend_op blend_op_alpha = blend_op::op_add;
};

enum class accel_struct_build_flags : uint8_t
{
    allow_update,
    allow_compaction,
    prefer_fast_trace,
    prefer_fast_build,
    minimize_memory
};

CC_FLAGS_ENUM(accel_struct_build_flags);
using accel_struct_build_flags_t = cc::flags<accel_struct_build_flags>;

/// geometry instance within a top level acceleration structure (layout dictated by DXR/Vulkan RT Extension)
struct accel_struct_geometry_instance
{
    /// Transform matrix, containing only the top 3 rows
    float transform[12];
    /// Instance index
    uint32_t instance_id : 24;
    /// Visibility mask
    uint32_t mask : 8;
    /// Index of the hit group which will be invoked when a ray hits the instance
    uint32_t instance_offset : 24;
    /// Instance flags, such as culling
    uint32_t flags : 8;
    /// Opaque handle of the bottom-level acceleration structure
    uint64_t native_accel_struct_handle;
};

static_assert(sizeof(accel_struct_geometry_instance) == 64, "accel_struct_geometry_instance compiles to incorrect size");

// these flags align exactly with both vulkan and d3d12, and are not translated
using accel_struct_instance_flags_t = uint32_t;
namespace accel_struct_instance_flags
{
enum accel_struct_instance_flags_e : accel_struct_instance_flags_t
{
    none = 0x0000,
    triangle_cull_disable = 0x0001,
    triangle_front_counterclockwise = 0x0002,
    force_opaque = 0x0004,
    force_no_opaque = 0x0008
};
}

/// the size and element-strides of a shader table
struct shader_table_sizes
{
    uint32_t ray_gen_stride_bytes = 0;
    uint32_t miss_stride_bytes = 0;
    uint32_t hit_group_stride_bytes = 0;
};
}
