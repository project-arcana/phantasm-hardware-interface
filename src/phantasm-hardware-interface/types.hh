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

/// synchronization primitive storing a uint64, can be signalled and waited on from both CPU and GPU
PHI_DEFINE_HANDLE(fence);

/// raytracing acceleration structure handle
PHI_DEFINE_HANDLE(accel_struct);

#undef PHI_DEFINE_HANDLE
}

/// resources bound to a shader, up to 4 per draw or dispatch command
struct shader_argument
{
    handle::resource constant_buffer;
    handle::shader_view shader_view;
    unsigned constant_buffer_offset;
};

enum class shader_stage : uint8_t
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

using shader_stage_flags_t = cc::flags<shader_stage, 16>;
CC_FLAGS_ENUM_SIZED(shader_stage, 16);

inline constexpr shader_stage_flags_t shader_stage_mask_all_graphics
    = shader_stage::vertex | shader_stage::hull | shader_stage::domain | shader_stage::geometry | shader_stage::pixel;

inline constexpr shader_stage_flags_t shader_stage_mask_all_ray
    = shader_stage::ray_gen | shader_stage::ray_miss | shader_stage::ray_closest_hit | shader_stage::ray_intersect | shader_stage::ray_any_hit;

enum class queue_type : uint8_t
{
    direct, // graphics + copy + compute + present
    compute,
    copy
};

/// state of a handle::resource, determining legal operations
/// (D3D12: resource states, Vulkan: access masks, image layouts and pipeline stage dependencies)
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

/// pixel format of a texture, or texture view (DXGI_FORMAT / VkFormat)
/// [f]loat, [i]nt, [u]int, [un]orm, [uf]loat, [t]ypeless
enum class format : uint8_t
{
    // regular formats
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

    // swizzled and irregular formats
    bgra8un,
    b10g11r11uf,

    // compressed formats
    bc6h_16f,
    bc6h_16uf,

    // view-only formats - depth
    r24un_g8t, // view the depth part of depth24un_stencil8u
    r24t_g8u,  // view the stencil part of depth24un_stencil8u

    // depth formats
    depth32f,
    depth16un,

    // depth stencil formats
    depth32f_stencil8u,
    depth24un_stencil8u,
};

/// returns true if the format is a view-only format
[[nodiscard]] constexpr bool is_view_format(format fmt) { return fmt >= format::r24un_g8t && fmt < format::depth32f; }

/// returns true if the format is a depth OR depth stencil format
[[nodiscard]] constexpr bool is_depth_format(format fmt) { return fmt >= format::depth32f; }

/// returns true if the format is a depth stencil format
[[nodiscard]] constexpr bool is_depth_stencil_format(format fmt) { return fmt >= format::depth32f_stencil8u; }

/// information about a single vertex attribute
struct vertex_attribute_info
{
    char const* semantic_name;
    unsigned offset;
    format fmt;
};

enum class texture_dimension : uint8_t
{
    t1d,
    t2d,
    t3d
};

/// the type of a resource_view
enum class resource_view_dimension : uint8_t
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

/// describes an element (either SRV or UAV) of a handle::shader_view
struct resource_view
{
    handle::resource resource;

    format pixel_format;
    resource_view_dimension dimension;

    struct texture_info_t
    {
        unsigned mip_start;   ///< index of the first usable mipmap (usually: 0)
        unsigned mip_size;    ///< amount of usable mipmaps, starting from mip_start (usually: -1 / all)
        unsigned array_start; ///< index of the first usable array element [if applicable] (usually: 0)
        unsigned array_size;  ///< amount of usable array elements [if applicable]
    };

    struct buffer_info_t
    {
        unsigned element_start;        ///< index of the first element in the buffer
        unsigned num_elements;         ///< amount of elements in the buffer
        unsigned element_stride_bytes; ///< the stride of elements in bytes
    };

    union {
        texture_info_t texture_info;
        buffer_info_t buffer_info;
    };

public:
    // convenience

    void init_as_null() { resource = handle::null_resource; }

    void init_as_backbuffer(handle::resource res)
    {
        resource = res;
        pixel_format = format::bgra8un;
        dimension = resource_view_dimension::texture2d;
        // cmdlist translation checks for this case and automatically chooses the right texture_info contents,
        // no need to specify
    }

    void init_as_tex2d(handle::resource res, format pf, bool multisampled = false, unsigned mip_start = 0, unsigned mip_size = unsigned(-1))
    {
        resource = res;
        pixel_format = pf;
        dimension = multisampled ? resource_view_dimension::texture2d_ms : resource_view_dimension::texture2d;
        texture_info.mip_start = mip_start;
        texture_info.mip_size = mip_size;
        texture_info.array_start = 0;
        texture_info.array_size = 1;
    }

    void init_as_texcube(handle::resource res, format pf)
    {
        resource = res;
        pixel_format = pf;
        dimension = resource_view_dimension::texturecube;
        texture_info.mip_start = 0;
        texture_info.mip_size = unsigned(-1);
        texture_info.array_start = 0;
        texture_info.array_size = 1;
    }

    void init_as_structured_buffer(handle::resource res, unsigned num_elements, unsigned stride_bytes)
    {
        resource = res;
        dimension = resource_view_dimension::buffer;
        buffer_info.num_elements = num_elements;
        buffer_info.element_start = 0;
        buffer_info.element_stride_bytes = stride_bytes;
    }

    /// receive the buffer handle from getAccelStructBuffer
    void init_as_accel_struct(handle::resource as_buffer)
    {
        resource = as_buffer;
        dimension = resource_view_dimension::raytracing_accel_struct;
    }

public:
    // static convenience

    static resource_view null()
    {
        resource_view rv;
        rv.init_as_null();
        return rv;
    }
    static resource_view backbuffer(handle::resource res)
    {
        resource_view rv;
        rv.init_as_backbuffer(res);
        return rv;
    }
    static resource_view tex2d(handle::resource res, format pf, bool multisampled = false, unsigned mip_start = 0, unsigned mip_size = unsigned(-1))
    {
        resource_view rv;
        rv.init_as_tex2d(res, pf, multisampled, mip_start, mip_size);
        return rv;
    }
    static resource_view texcube(handle::resource res, format pf)
    {
        resource_view rv;
        rv.init_as_texcube(res, pf);
        return rv;
    }
    static resource_view structured_buffer(handle::resource res, unsigned num_elements, unsigned stride_bytes)
    {
        resource_view rv;
        rv.init_as_structured_buffer(res, num_elements, stride_bytes);
        return rv;
    }
    static resource_view accel_struct(handle::resource as_buffer)
    {
        resource_view rv;
        rv.init_as_accel_struct(as_buffer);
        return rv;
    }
};

/// the texture filtering mode of a sampler
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

/// the texture addressing mode (U/V/W) of a sampler
enum class sampler_address_mode : uint8_t
{
    wrap,
    clamp,
    clamp_border,
    mirror
};

/// the comparison function of a sampler
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

/// the border color of a sampler (with address mode clamp_border)
enum class sampler_border_color : uint8_t
{
    black_transparent_float,
    black_transparent_int,
    black_float,
    black_int,
    white_float,
    white_int
};

/// configuration from which a sampler is created, as part of a handle::shader_view
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
    sampler_border_color border_color; ///< the border color to use, req. sampler_address_mode::clamp_border

    void init_default(sampler_filter filter, unsigned anisotropy = 16u, sampler_address_mode address_mode = sampler_address_mode::wrap)
    {
        this->filter = filter;
        address_u = address_mode;
        address_v = address_mode;
        address_w = address_mode;
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

/// the structure of vertices a handle::pipeline_state takes in
enum class primitive_topology : uint8_t
{
    triangles,
    lines,
    points,
    patches
};

/// the depth function a handle::pipeline_state is using
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

/// the face culling mode a handle::pipeline_state is using
enum class cull_mode : uint8_t
{
    none,
    back,
    front
};

/// configuration for creation of a (graphics) handle::pipeline_state
struct pipeline_config
{
    primitive_topology topology = primitive_topology::triangles;
    depth_function depth = depth_function::none;
    bool depth_readonly = false;
    cull_mode cull = cull_mode::none;
    int samples = 1;
    bool conservative_raster = false;
};

/// operation to perform on render targets upon render pass begin
enum class rt_clear_type : uint8_t
{
    clear,
    dont_care,
    load
};

/// value to clear a render target with
union rt_clear_value {
    float color[4];
    struct
    {
        float depth;
        uint8_t stencil;
    } depth_stencil;

    rt_clear_value() = default;

    rt_clear_value(float r, float g, float b, float a)
    {
        color[0] = r;
        color[1] = g;
        color[2] = b;
        color[3] = a;
    }

    rt_clear_value(float depth, uint8_t stencil)
    {
        depth_stencil.depth = depth;
        depth_stencil.stencil = stencil;
    }
};

/// blending logic operation a (graphics) handle::pipeline_state performs on its render targets
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

/// blending operation a (graphics) handle::pipeline_state performs on a specific render target slot
enum class blend_op : uint8_t
{
    op_add,
    op_subtract,
    op_reverse_subtract,
    op_min,
    op_max
};

/// the source or destination blend factor of a blending operation on a specific render target slot
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

struct blend_state
{
    blend_factor blend_color_src = blend_factor::one;
    blend_factor blend_color_dest = blend_factor::zero;
    blend_op blend_op_color = blend_op::op_add;
    blend_factor blend_alpha_src = blend_factor::one;
    blend_factor blend_alpha_dest = blend_factor::zero;
    blend_op blend_op_alpha = blend_op::op_add;

    blend_state() = default;

    blend_state(blend_factor blend_color_src, //
                blend_factor blend_color_dest,
                blend_op blend_op_color,
                blend_factor blend_alpha_src,
                blend_factor blend_alpha_dest,
                blend_op blend_op_alpha)
      : blend_color_src(blend_color_src), //
        blend_color_dest(blend_color_dest),
        blend_op_color(blend_op_color),
        blend_alpha_src(blend_alpha_src),
        blend_alpha_dest(blend_alpha_dest),
        blend_op_alpha(blend_op_alpha)
    {
    }

    blend_state(blend_factor blend_color_src, //
                blend_factor blend_color_dest,
                blend_factor blend_alpha_src,
                blend_factor blend_alpha_dest)
      : blend_color_src(blend_color_src), //
        blend_color_dest(blend_color_dest),
        blend_op_color(blend_op::op_add),
        blend_alpha_src(blend_alpha_src),
        blend_alpha_dest(blend_alpha_dest),
        blend_op_alpha(blend_op::op_add)
    {
    }

    blend_state(blend_factor blend_src, //
                blend_factor blend_dest,
                blend_op blend_op = blend_op::op_add)
      : blend_color_src(blend_src), //
        blend_color_dest(blend_dest),
        blend_op_color(blend_op),
        blend_alpha_src(blend_src),
        blend_alpha_dest(blend_dest),
        blend_op_alpha(blend_op)
    {
    }

    /// blend state for additive blending "src + dest"
    static blend_state additive() { return blend_state(blend_factor::one, blend_factor::one); }

    /// blend state for multiplicative blending "src * dest"
    static blend_state multiplicative()
    {
        return blend_state(blend_factor::dest_color, blend_factor::zero, blend_factor::dest_alpha, blend_factor::zero);
    }

    /// blend state for normal alpha blending "mix(dest, src, src.a)"
    static blend_state alpha_blending() { return blend_state(blend_factor::src_alpha, blend_factor::inv_src_alpha); }

    /// blend state for premultiplied alpha blending "dest * (1 - src.a) + src"
    static blend_state alpha_blending_premultiplied() { return blend_state(blend_factor::one, blend_factor::inv_src_alpha); }
};

/// the blending configuration for a specific render target slot of a (graphics) handle::pipeline_state
struct render_target_config
{
    format fmt = format::rgba8un;
    bool blend_enable = false;
    blend_state state;
};

/// flags to configure the building process of a raytracing acceleration structure
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

/// the size and element-strides of a raytracing shader table
struct shader_table_sizes
{
    uint32_t ray_gen_stride_bytes = 0;
    uint32_t miss_stride_bytes = 0;
    uint32_t hit_group_stride_bytes = 0;
};
}
