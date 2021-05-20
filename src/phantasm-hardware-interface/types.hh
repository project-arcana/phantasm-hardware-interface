#pragma once

#include <cstdint>

#include <clean-core/flags.hh>

#include <phantasm-hardware-interface/handles.hh>

#define PHI_DEFINE_FLAG_TYPE(_flags_t_, _enum_t_, _max_num_)    \
    using _flags_t_ = cc::flags<_enum_t_, uint32_t(_max_num_)>; \
    CC_FLAGS_ENUM_SIZED(_enum_t_, uint32_t(_max_num_));

#define PHI_DEFINE_FLAG_OPERATORS(FlagT, RealT)                                                                  \
    [[maybe_unused]] constexpr FlagT operator|(FlagT a, FlagT b) noexcept { return FlagT(RealT(a) | RealT(b)); } \
    [[maybe_unused]] constexpr FlagT operator&(FlagT a, FlagT b) noexcept { return FlagT(RealT(a) & RealT(b)); } \
    [[maybe_unused]] constexpr FlagT operator^(FlagT a, FlagT b) noexcept { return FlagT(RealT(a) ^ RealT(b)); } \
    [[maybe_unused]] constexpr FlagT operator~(FlagT a) noexcept { return FlagT(~RealT(a)); }

namespace phi
{
/// resources bound to a shader, up to 4 per draw or dispatch command
struct shader_argument
{
    handle::resource constant_buffer;
    handle::shader_view shader_view;
    uint32_t constant_buffer_offset;
};

/// the type of a single shader
enum class shader_stage : uint8_t
{
    none = 0,

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
    ray_callable,

    MAX_SHADER_STAGE_RANGE,
    NUM_SHADER_STAGES = MAX_SHADER_STAGE_RANGE - 1
};

constexpr bool is_valid_shader_stage(shader_stage s) { return s > shader_stage::none && s < shader_stage::MAX_SHADER_STAGE_RANGE; }

PHI_DEFINE_FLAG_TYPE(shader_stage_flags_t, shader_stage, shader_stage::NUM_SHADER_STAGES);

inline constexpr shader_stage_flags_t shader_stage_mask_all_graphics
    = shader_stage::vertex | shader_stage::hull | shader_stage::domain | shader_stage::geometry | shader_stage::pixel;

inline constexpr shader_stage_flags_t shader_stage_mask_all_ray = shader_stage::ray_gen | shader_stage::ray_miss | shader_stage::ray_closest_hit
                                                                  | shader_stage::ray_intersect | shader_stage::ray_any_hit | shader_stage::ray_callable;

inline constexpr shader_stage_flags_t shader_stage_mask_ray_identifiable = shader_stage::ray_gen | shader_stage::ray_miss | shader_stage::ray_callable;

inline constexpr shader_stage_flags_t shader_stage_mask_ray_hitgroup = shader_stage::ray_closest_hit | shader_stage::ray_any_hit | shader_stage::ray_intersect;

enum class queue_type : uint8_t
{
    direct, // graphics + copy + compute + present
    compute,
    copy
};

/// the swapchain presentation mode
enum class present_mode : uint8_t
{
    synced,                 // synchronize presentation every vblank
    synced_2nd_vblank,      // synchronize presentation every second vblank (effectively halves refreshrate)
    unsynced,               // do not synchronize presentation
    unsynced_allow_tearing, // do not synchronize presentation and allow tearing, required for variable refresh rate displays
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

    constant_buffer,          // accessed via a CBV in any shader
    shader_resource,          // accessed via a SRV in any shader
    shader_resource_nonpixel, // accessed via a SRV in a non-pixel shader only
    unordered_access,         // accessed via a UAV in any shader

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

/// information describing a single resource transition, specifying only the target state
struct transition_info
{
    handle::resource resource;              ///< the resource to transition
    resource_state target_state;            ///< the state the resource is transitioned into
    shader_stage_flags_t dependent_shaders; ///< the shader stages accessing the resource afterwards, only applies to CBV, SRV and UAV states
};

enum class resource_heap : uint8_t
{
    gpu,     // default, fastest to access for the GPU
    upload,  // for CPU -> GPU transfer
    readback // for GPU -> CPU transfer
};

/// pixel format of a texture, or texture view (DXGI_FORMAT / VkFormat)
/// [f]loat, [i]nt, [u]int, [un]orm, [uf]loat, [t]ypeless
enum class format : uint8_t
{
    none = 0,

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

    rgba16un,
    rg16un,
    r16un,

    rgba8i,
    rg8i,
    r8i,

    rgba8u,
    rg8u,
    r8u,

    rgba8un,
    rg8un,
    r8un,

    // sRGB versions of regular formats
    rgba8un_srgb,

    // swizzled and irregular formats
    bgra8un,
    bgra4un,
    b10g11r11uf,
    r10g10b10a2u,
    r10g10b10a2un,
    b5g6r5un,
    b5g5r5a1un,
    r9g9b9e5_sharedexp_uf, // three ufloats sharing a single 5 bit exponent, 32b in total

    // block-compressed formats
    bc1,
    bc1_srgb,
    bc2,
    bc2_srgb,
    bc3,
    bc3_srgb,
    bc6h_16f,
    bc6h_16uf,
    bc7,
    bc7_srgb,

    // view-only formats - depth
    r24un_g8t, // view the depth part of depth24un_stencil8u
    r24t_g8u,  // view the stencil part of depth24un_stencil8u

    // depth formats
    depth32f,
    depth16un,

    // depth stencil formats
    depth32f_stencil8u,
    depth24un_stencil8u,

    MAX_FORMAT_RANGE,
    NUM_FORMATS = MAX_FORMAT_RANGE - 1
};

constexpr bool is_valid_format(format fmt) { return fmt > format::none && fmt < format::MAX_FORMAT_RANGE; }

/// information about a single vertex attribute
struct vertex_attribute_info
{
    char const* semantic_name = nullptr;
    uint32_t offset = 0;
    format fmt = format::none;
    uint8_t vertex_buffer_i = 0;
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
    none = 0,
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
    raytracing_accel_struct,

    MAX_DIMENSION_RANGE,
    NUM_DIMENSIONS = MAX_DIMENSION_RANGE - 1
};

/// describes an element (either SRV or UAV) of a handle::shader_view
struct resource_view
{
    handle::resource resource;

    resource_view_dimension dimension;

    struct texture_info_t
    {
        format pixel_format;
        uint32_t mip_start;   ///< index of the first usable mipmap (usually: 0)
        uint32_t mip_size;    ///< amount of usable mipmaps, starting from mip_start (usually: -1 / all)
        uint32_t array_start; ///< index of the first usable array element [if applicable] (usually: 0)
        uint32_t array_size;  ///< amount of usable array elements [if applicable]
    };

    struct buffer_info_t
    {
        uint32_t element_start;        ///< index of the first element in the buffer
        uint32_t num_elements;         ///< amount of elements in the buffer
        uint32_t element_stride_bytes; ///< the stride of elements in bytes
    };

    struct accel_struct_info_t
    {
        handle::accel_struct accel_struct;
    };

    union
    {
        texture_info_t texture_info;
        buffer_info_t buffer_info;
        accel_struct_info_t accel_struct_info;
    };

public:
    // convenience

    void init_as_null()
    {
        dimension = resource_view_dimension::none;
        resource = handle::null_resource;
    }

    void init_as_backbuffer(handle::resource res)
    {
        dimension = resource_view_dimension::texture2d;
        resource = res;
        texture_info.pixel_format = format::bgra8un;
        // cmdlist translation checks for this case and automatically chooses the right texture_info contents,
        // no need to specify
    }

    void init_as_tex2d(handle::resource res, format pf, bool multisampled = false, uint32_t mip_slice = 0)
    {
        dimension = multisampled ? resource_view_dimension::texture2d_ms : resource_view_dimension::texture2d;
        resource = res;
        texture_info.pixel_format = pf;
        texture_info.mip_start = mip_slice;
        texture_info.mip_size = 1;
        texture_info.array_start = 0;
        texture_info.array_size = 1;
    }

    void init_as_tex2d_array(handle::resource res, format pf, bool multisampled, uint32_t array_start = 0, uint32_t array_size = 1, uint32_t mip_slice = 0)
    {
        dimension = multisampled ? resource_view_dimension::texture2d_ms_array : resource_view_dimension::texture2d_array;
        resource = res;
        texture_info.pixel_format = pf;
        texture_info.mip_start = mip_slice;
        texture_info.mip_size = 1;
        texture_info.array_start = array_start;
        texture_info.array_size = array_size;
    }

    void init_as_tex3d(handle::resource res, format pf, uint32_t array_start, uint32_t array_size, uint32_t mip_slice = 0)
    {
        dimension = resource_view_dimension::texture3d;
        resource = res;
        texture_info.pixel_format = pf;
        texture_info.mip_start = mip_slice;
        texture_info.mip_size = 1;
        texture_info.array_start = array_start;
        texture_info.array_size = array_size;
    }

    void init_as_texcube(handle::resource res, format pf)
    {
        dimension = resource_view_dimension::texturecube;
        resource = res;
        texture_info.pixel_format = pf;
        texture_info.mip_start = 0;
        texture_info.mip_size = uint32_t(-1);
        texture_info.array_start = 0;
        texture_info.array_size = 1;
    }

    void init_as_structured_buffer(handle::resource res, uint32_t num_elements, uint32_t stride_bytes, uint32_t element_start = 0)
    {
        dimension = resource_view_dimension::buffer;
        resource = res;
        buffer_info.num_elements = num_elements;
        buffer_info.element_start = element_start;
        buffer_info.element_stride_bytes = stride_bytes;
    }

    void init_as_accel_struct(handle::accel_struct as)
    {
        dimension = resource_view_dimension::raytracing_accel_struct;
        resource = handle::null_resource;
        accel_struct_info.accel_struct = as;
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
    static resource_view tex2d(handle::resource res, format pf, bool multisampled = false, uint32_t mip_slice = 0)
    {
        resource_view rv;
        rv.init_as_tex2d(res, pf, multisampled, mip_slice);
        return rv;
    }
    static resource_view texcube(handle::resource res, format pf)
    {
        resource_view rv;
        rv.init_as_texcube(res, pf);
        return rv;
    }
    static resource_view structured_buffer(handle::resource res, uint32_t num_elements, uint32_t stride_bytes)
    {
        resource_view rv;
        rv.init_as_structured_buffer(res, num_elements, stride_bytes);
        return rv;
    }
    static resource_view accel_struct(handle::accel_struct as)
    {
        resource_view rv;
        rv.init_as_accel_struct(as);
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
    uint32_t max_anisotropy; ///< maximum amount of anisotropy in [1, 16], req. sampler_filter::anisotropic
    sampler_compare_func compare_func;
    sampler_border_color border_color; ///< the border color to use, req. sampler_address_mode::clamp_border

    void init_default(sampler_filter filter, uint32_t anisotropy = 16u, sampler_address_mode address_mode = sampler_address_mode::wrap)
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

    sampler_config(sampler_filter filter, uint32_t anisotropy = 16u, sampler_address_mode address_mode = sampler_address_mode::wrap)
    {
        init_default(filter, anisotropy, address_mode);
    }
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
    int32_t samples = 1;
    bool conservative_raster = false;
    bool frontface_counterclockwise = true; // TODO: this default should be flipped
    bool wireframe = false;
};

/// operation to perform on render targets upon render pass begin
enum class rt_clear_type : uint8_t
{
    clear,
    dont_care,
    load
};

/// value to clear a render target with
struct rt_clear_value
{
    uint8_t red_or_depth;
    uint8_t green_or_stencil;
    uint8_t blue;
    uint8_t alpha;

    rt_clear_value() = default;
    rt_clear_value(float r, float g, float b, float a)
      : red_or_depth(uint8_t(r * 255)), green_or_stencil(uint8_t(g * 255)), blue(uint8_t(b * 255)), alpha(uint8_t(a * 255))
    {
    }
    rt_clear_value(float depth, uint8_t stencil) : red_or_depth(uint8_t(depth * 255)), green_or_stencil(stencil), blue(0), alpha(0) {}

    static rt_clear_value from_uint(uint32_t value)
    {
        rt_clear_value res;
        res.red_or_depth = uint8_t(value >> 24);
        res.green_or_stencil = uint8_t(value >> 16);
        res.blue = uint8_t(value >> 8);
        res.alpha = uint8_t(value);
        return res;
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

public:
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

/// the type of a handle::query_range
enum class query_type : uint8_t
{
    timestamp,
    occlusion,
    pipeline_stats
};

/// a single signal- or wait operation on a fence
struct fence_operation
{
    handle::fence fence;
    uint64_t value;
};

/// indirect draw command, as it is laid out in a GPU buffer
struct gpu_indirect_command_draw
{
    uint32_t num_vertices;
    uint32_t num_instances;
    uint32_t vertex_offset;
    uint32_t instance_offset;
};

/// indirect indexed draw command, as it is laid out in a GPU buffer
struct gpu_indirect_command_draw_indexed
{
    uint32_t num_indices;
    uint32_t num_instances;
    uint32_t index_offset;
    int32_t vertex_offset;
    uint32_t instance_offset;
};

/// indirect compute dispatch command, as it is laid out in a GPU buffer
struct gpu_indirect_command_dispatch
{
    uint32_t dispatch_x;
    uint32_t dispatch_y;
    uint32_t dispatch_z;
};

struct resource_usage_flags
{
    enum : uint32_t
    {
        none = 0,
        allow_uav = 1 << 0,
        allow_render_target = 1 << 1,
        allow_depth_stencil = 1 << 2,
        deny_shader_resource = 1 << 3,
        use_optimized_clear_value = 1 << 4,
    };
};
using resource_usage_flags_t = uint32_t;

// PHI_DEFINE_FLAG_OPERATORS(resource_usage_flags, uint32_t)

/// flags to configure the building process of a raytracing acceleration structure
enum class accel_struct_build_flags : uint8_t
{
    allow_update,
    allow_compaction,
    prefer_fast_trace,
    prefer_fast_build,
    minimize_memory
};

PHI_DEFINE_FLAG_TYPE(accel_struct_build_flags_t, accel_struct_build_flags, 8);

// these flags align exactly with both vulkan and d3d12, and are not translated
struct accel_struct_instance_flags
{
    enum : uint32_t
    {
        none = 0,
        triangle_cull_disable = 1 << 0,
        triangle_front_counterclockwise = 1 << 1,
        force_opaque = 1 << 2,
        force_no_opaque = 1 << 3
    };
};
using accel_struct_instance_flags_t = uint32_t;

/// bottom level accel struct instance within a top level accel struct (layout dictated by DXR/Vulkan RT Extension)
struct accel_struct_instance
{
    /// Transposed transform matrix, containing only the top 3 rows (laid out as three 4-vectors)
    float transposed_transform[12];

    /// Instance id - arbitrary value, accessed in shaders via `InstanceID()` (HLSL)
    uint32_t instance_id : 24;

    /// Visibility mask - matched against `InstanceInclusionMask` parameter in `TraceRays(..)` (HLSL)
    uint32_t visibility_mask : 8;

    /// Index of the hit group which will be invoked when a ray hits the instance
    uint32_t hit_group_index : 24;

    /// Instance flags, such as culling
    accel_struct_instance_flags_t flags : 8;

    /// Opaque handle of the bottom-level acceleration structure,
    /// as received from `out_native_handle` in `createBottomLevelAccelStruct` (phi Backend)
    uint64_t native_bottom_level_as_handle;
};

static_assert(sizeof(accel_struct_instance) == 64, "accel_struct_instance compiles to incorrect size");

struct buffer_address
{
    handle::resource buffer = handle::null_resource;
    uint32_t offset_bytes = 0;
};

struct buffer_range
{
    handle::resource buffer = handle::null_resource;
    uint32_t offset_bytes = 0;
    uint32_t size_bytes = 0;
};

struct buffer_range_and_stride
{
    handle::resource buffer = handle::null_resource;
    uint32_t offset_bytes = 0;
    uint32_t size_bytes = 0;
    uint32_t stride_bytes = 0;
};

/// the sizes required for the four sections of a raytracing shader table
struct shader_table_strides
{
    // ray_gen: record size
    uint32_t size_ray_gen = 0;
    // miss, hitgroup, callable: full sizes and strides (record sizes)
    uint32_t size_miss = 0;
    uint32_t stride_miss = 0;
    uint32_t size_hit_group = 0;
    uint32_t stride_hit_group = 0;
    uint32_t size_callable = 0;
    uint32_t stride_callable = 0;
};

struct vram_state_info
{
    // OS-provided VRAM budget in bytes, usage should stay below this
    uint64_t os_budget_bytes = 0;
    uint64_t current_usage_bytes = 0;
    uint64_t available_for_reservation_bytes = 0;
    uint64_t current_reservation_bytes = 0;
};
}
