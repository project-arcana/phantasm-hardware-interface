#pragma once

#include <stdint.h>
#include <string.h>

#include <phantasm-hardware-interface/handles.hh>

namespace phi
{
using bool32_t = uint32_t;

// resources bound to a shader, up to 4 per draw or dispatch command
struct shader_argument
{
    handle::resource constant_buffer = handle::null_resource;
    handle::shader_view shader_view = handle::null_shader_view;
    uint32_t constant_buffer_offset = 0;
};

// the type of a single shader
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

using shader_stage_flags_t = uint16_t;
struct shader_stage_flags
{
    enum : shader_stage_flags_t
    {
        none = 0,

        // graphics
        vertex = 1 << 0,
        hull = 1 << 1,
        domain = 1 << 2,
        geometry = 1 << 3,
        pixel = 1 << 4,

        // compute
        compute = 1 << 5,

        // raytracing
        ray_gen = 1 << 6,
        ray_miss = 1 << 7,
        ray_closest_hit = 1 << 8,
        ray_intersect = 1 << 9,
        ray_any_hit = 1 << 10,
        ray_callable = 1 << 11,

        MASK_all_graphics = vertex | hull | domain | geometry | pixel,
        MASK_ray_identifiable = ray_gen | ray_miss | ray_callable,
        MASK_ray_hitgroup = ray_closest_hit | ray_any_hit | ray_intersect,
        MASK_all_ray = MASK_ray_identifiable | MASK_ray_hitgroup,
    };
};

constexpr bool is_valid_shader_stage(shader_stage s) { return s > shader_stage::none && s < shader_stage::MAX_SHADER_STAGE_RANGE; }

constexpr shader_stage_flags_t to_shader_stage_flags(shader_stage s)
{
    return s == shader_stage::none ? 0 : shader_stage_flags_t(1 << (uint8_t(s) - 1));
}

enum class queue_type : uint8_t
{
    direct, // graphics + copy + compute + present
    compute,
    copy
};

// the swapchain presentation mode
enum class present_mode : uint8_t
{
    none = 0,

    synced,                 // synchronize presentation every vblank
    synced_2nd_vblank,      // synchronize presentation every second vblank (effectively halves refreshrate)
    unsynced,               // do not synchronize presentation
    unsynced_allow_tearing, // do not synchronize presentation and allow tearing, required for variable refresh rate displays
};

// state of a handle::resource, determining legal operations
// (D3D12: resource states, Vulkan: access masks, image layouts and pipeline stage dependencies)
enum class resource_state : uint32_t
{
    // unknown to pr
    unknown = 0,
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

// a single signal- or wait operation on a fence
struct fence_operation
{
    handle::fence fence = handle::null_fence;
    uint64_t value = 0;
};

// memhashed structs that must not be padded, C4820: warning about padding
// #pragma warning(error: 4820): enable C4820 and promote it to an error
#ifdef CC_COMPILER_MSVC
#pragma warning(push)
#pragma warning(error : 4820)
#endif

// information describing a single resource transition, specifying only the target state
struct transition_info
{
    handle::resource resource = handle::null_resource;       //< the resource to transition
    resource_state target_state = resource_state::undefined; //< the state the resource is transitioned into
    shader_stage_flags_t dependent_shaders = shader_stage_flags::none; //< the shader stages accessing the resource afterwards, only applies to CBV, SRV and UAV states
    uint16_t _pad0 = 0;
};

enum class resource_heap : uint8_t
{
    none = 0,

    gpu,     // default, fastest to access for the GPU
    upload,  // for CPU -> GPU transfer
    readback // for GPU -> CPU transfer
};

// pixel format of a texture, or texture view (DXGI_FORMAT / VkFormat)
// [f]loat, [i]nt, [u]int, [un]orm, [uf]loat, [t]ypeless
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
    bc1,       // BC1: 4x4 rgb8 pixels in 64 bit (6:1), DXT1
    bc1_srgb,  // sRGB version of BC1
    bc2,       // BC2: 4x4 rgba8 (premultiplied) pixels in 128 bit (4:1), DXT2
    bc2_srgb,  // sRGB version of BC2
    bc3,       // BC3: 4x4 rgba8 (non-premultiplied) pixels in 128 bit (4:1), DXT3
    bc3_srgb,  // sRGB version of BC3
    bc6h_16f,  // BC6H: 4x4 rgb16f pixels in 128 bit (6:1)
    bc6h_16uf, // unsigned float version of BC6H
    bc7,       // BC7: 4x4 rgba8 pixels in 128bit (4:1)
    bc7_srgb,  // sRGB version of BC7

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

// information about a single vertex attribute
struct vertex_attribute_info
{
    char const* semantic_name = nullptr;
    uint32_t offset = 0;
    format fmt = format::none;
    uint8_t vertex_buffer_i = 0;
    uint16_t _pad0 = 0;
};

enum class texture_dimension : uint8_t
{
    none = 0,

    t1d,
    t2d,
    t3d
};

// the type of a resource_view
enum class resource_view_dimension : uint32_t
{
    none = 0,

    // [RW]Buffer, [RW]StructuredBuffer
    buffer,
    // [RW]ByteAddressBuffer
    raw_buffer,
    // [RW]Texture1D
    texture1d,
    // [RW]Texture1DArray
    texture1d_array,
    // [RW]Texture2D
    texture2d,
    // [RW]Texture2DMS (multisampled)
    texture2d_ms,
    // [RW]Texture2DArray
    texture2d_array,
    // [RW]Texture2DMSArray (multisampled)
    texture2d_ms_array,
    // [RW]Texture3D
    texture3d,
    // TextureCube (SRV only)
    texturecube,
    // TextureCubeArray (SRV only)
    texturecube_array,
    // RaytracingAccelerationStructure (SRV only)
    raytracing_accel_struct,

    MAX_DIMENSION_RANGE,
    NUM_DIMENSIONS = MAX_DIMENSION_RANGE - 1
};

// describes an element (either SRV or UAV) of a handle::shader_view
struct resource_view
{
    handle::resource resource = handle::null_resource;
    resource_view_dimension dimension = resource_view_dimension::none;

    struct texture_info_t
    {
        format pixel_format;
        uint8_t _pad0[3];
        uint32_t mip_start;   // index of the first usable mipmap (usually: 0)
        uint32_t mip_size;    // amount of usable mipmaps, starting from mip_start (usually: -1 / all)
        uint32_t array_start; // index of the first usable array element [if applicable] (usually: 0)
        uint32_t array_size;  // amount of usable array elements [if applicable]
    };

    struct buffer_info_t
    {
        uint32_t element_start;        // index of the first element in the buffer (for raw buffers, the first byte)
        uint32_t num_elements;         // amount of elements in the buffer (for raw buffers, amount of bytes)
        uint32_t element_stride_bytes; // the stride of elements in bytes (for raw buffers ignored)
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

    constexpr resource_view() : resource(handle::null_resource), dimension(resource_view_dimension::none), texture_info() {}

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

    void init_as_tex2d(handle::resource res, format pf, bool multisampled = false, uint32_t mip_start = 0, uint32_t num_mips = uint32_t(-1))
    {
        dimension = multisampled ? resource_view_dimension::texture2d_ms : resource_view_dimension::texture2d;
        resource = res;
        texture_info.pixel_format = pf;
        texture_info.mip_start = mip_start;
        texture_info.mip_size = num_mips;
        texture_info.array_start = 0;
        texture_info.array_size = 1;
    }

    void init_as_tex2d_array(
        handle::resource res, format pf, bool multisampled, uint32_t array_start = 0, uint32_t array_size = 1, uint32_t mip_start = 0, uint32_t num_mips = uint32_t(-1))
    {
        dimension = multisampled ? resource_view_dimension::texture2d_ms_array : resource_view_dimension::texture2d_array;
        resource = res;
        texture_info.pixel_format = pf;
        texture_info.mip_start = mip_start;
        texture_info.mip_size = num_mips;
        texture_info.array_start = array_start;
        texture_info.array_size = array_size;
    }

    void init_as_tex3d(handle::resource res, format pf, uint32_t array_start, uint32_t array_size, uint32_t mip_slice = 0, uint32_t num_mips = uint32_t(-1))
    {
        dimension = resource_view_dimension::texture3d;
        resource = res;
        texture_info.pixel_format = pf;
        texture_info.mip_start = mip_slice;
        texture_info.mip_size = num_mips;
        texture_info.array_start = array_start;
        texture_info.array_size = array_size;
    }

    void init_as_texcube(handle::resource res, format pf, uint32_t mip_start = 0, uint32_t num_mips = uint32_t(-1))
    {
        dimension = resource_view_dimension::texturecube;
        resource = res;
        texture_info.pixel_format = pf;
        texture_info.mip_start = mip_start;
        texture_info.mip_size = num_mips;
        texture_info.array_start = 0;
        texture_info.array_size = 6;
    }

    void init_as_structured_buffer(handle::resource res, uint32_t num_elements, uint32_t stride_bytes, uint32_t element_start = 0)
    {
        dimension = resource_view_dimension::buffer;
        resource = res;
        buffer_info.num_elements = num_elements;
        buffer_info.element_start = element_start;
        buffer_info.element_stride_bytes = stride_bytes;
    }

    void init_as_byte_address_buffer(handle::resource res, uint32_t num_bytes, uint32_t offset_bytes = 0)
    {
        dimension = resource_view_dimension::raw_buffer;
        resource = res;
        buffer_info.num_elements = num_bytes;
        buffer_info.element_start = offset_bytes;
        buffer_info.element_stride_bytes = 0;
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

    static resource_view tex2d(handle::resource res, format pf, bool multisampled = false, uint32_t mip_start = 0, uint32_t num_mips = uint32_t(-1))
    {
        resource_view rv;
        rv.init_as_tex2d(res, pf, multisampled, mip_start, num_mips);
        return rv;
    }

    static resource_view tex3d(handle::resource tex, format fmt, uint32_t array_start = 0, uint32_t array_size = 1, uint32_t mip_start = 0, uint32_t num_mips = uint32_t(-1))
    {
        resource_view rv;
        rv.init_as_tex3d(tex, fmt, array_start, array_size, mip_start, num_mips);
        return rv;
    }

    static resource_view tex2d_array(
        handle::resource res, format pf, uint32_t array_start, uint32_t array_size, bool multisampled = false, uint32_t mip_start = 0, uint32_t num_mips = uint32_t(-1))
    {
        resource_view rv;
        rv.init_as_tex2d_array(res, pf, multisampled, array_start, array_size, mip_start, num_mips);
        return rv;
    }

    static resource_view texcube(handle::resource res, format pf, uint32_t mip_start = 0, uint32_t num_mips = uint32_t(-1))
    {
        resource_view rv;
        rv.init_as_texcube(res, pf, mip_start, num_mips);
        return rv;
    }
    static resource_view structured_buffer(handle::resource res, uint32_t num_elements, uint32_t stride_bytes, uint32_t element_start = 0)
    {
        resource_view rv;
        rv.init_as_structured_buffer(res, num_elements, stride_bytes, element_start);
        return rv;
    }
    static resource_view byte_address_buffer(handle::resource res, uint32_t num_bytes, uint32_t offset_bytes = 0)
    {
        resource_view rv;
        rv.init_as_byte_address_buffer(res, num_bytes, offset_bytes);
        return rv;
    }
    static resource_view accel_struct(handle::accel_struct as)
    {
        resource_view rv;
        rv.init_as_accel_struct(as);
        return rv;
    }
};

// the texture filtering mode of a sampler
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

// the texture addressing mode (U/V/W) of a sampler
enum class sampler_address_mode : uint8_t
{
    wrap,
    clamp,
    clamp_border,
    mirror
};

// the comparison function of a sampler
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

// the border color of a sampler (with address mode clamp_border)
enum class sampler_border_color : uint8_t
{
    black_transparent_float,
    black_transparent_int,
    black_float,
    black_int,
    white_float,
    white_int
};

// configuration from which a sampler is created, as part of a handle::shader_view
struct sampler_config
{
    sampler_filter filter = sampler_filter::min_mag_mip_linear;
    sampler_address_mode address_u = sampler_address_mode::wrap;
    sampler_address_mode address_v = sampler_address_mode::wrap;
    sampler_address_mode address_w = sampler_address_mode::wrap;
    float min_lod = 0.f;
    float max_lod = 100000.f;
    float lod_bias = 0.f;          //< offset from the calculated MIP level (sampled = calculated + lod_bias)
    uint32_t max_anisotropy = 16u; //< maximum amount of anisotropy in [1, 16], req. sampler_filter::anisotropic
    sampler_compare_func compare_func = sampler_compare_func::disabled;
    sampler_border_color border_color = sampler_border_color::white_float; //< the border color to use, req. sampler_address_mode::clamp_border
    uint8_t _pad0 = 0;
    uint8_t _pad1 = 0;

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

// the structure of vertices a handle::pipeline_state takes in
enum class primitive_topology : uint8_t
{
    triangles,
    lines,
    points,
    patches
};

// the depth function a handle::pipeline_state is using
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

// the face culling mode a handle::pipeline_state is using
enum class cull_mode : uint8_t
{
    none,
    back,
    front
};

// operation to perform on render targets upon render pass begin
enum class rt_clear_type : uint8_t
{
    clear,
    dont_care,
    load
};

// value to clear a render target with
struct rt_clear_value
{
    uint8_t red_or_depth = 0;
    uint8_t green_or_stencil = 0;
    uint8_t blue = 0;
    uint8_t alpha = 0;

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

// blending logic operation a (graphics) handle::pipeline_state performs on its render targets
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

// blending operation a (graphics) handle::pipeline_state performs on a specific render target slot
enum class blend_op : uint8_t
{
    op_add,
    op_subtract,
    op_reverse_subtract,
    op_min,
    op_max
};

// the source or destination blend factor of a blending operation on a specific render target slot
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

// the type of a handle::query_range
enum class query_type : uint8_t
{
    timestamp,
    occlusion,
    pipeline_stats
};

enum class indirect_command_type : uint8_t
{
    INVALID = 0,

    // array of gpu_indirect_command_draw structs
    draw,

    // array of gpu_indirect_command_draw_indexed structs
    draw_indexed,

    // array of gpu_indirect_command_draw_indexed_with_id structs
    draw_indexed_with_id,
};

// indirect draw command, as it is laid out in a GPU buffer
struct gpu_indirect_command_draw
{
    uint32_t num_vertices = 0;
    uint32_t num_instances = 0;
    uint32_t vertex_offset = 0;
    uint32_t first_instance = 0;
};

// indirect indexed draw command, as it is laid out in a GPU buffer
struct gpu_indirect_command_draw_indexed
{
    uint32_t num_indices = 0;
    uint32_t num_instances = 0;
    uint32_t index_offset = 0;
    int32_t vertex_offset = 0;
    uint32_t first_instance = 0;
};

// indirect indexed draw command with frontloaded custom root constant value, as it is laid out in a GPU buffer
// NOTE: draw_id_d3d12 overrides the first 4 bytes of root constants in D3D12 in order to provide a draw ID per call.
// In Vulkan, the field is unused and the draw ID can instead be written into first_instance, retrieved using SV_InstanceID.
struct gpu_indirect_command_draw_indexed_with_id
{
    uint32_t draw_id_d3d12 = 0;
    uint32_t num_indices = 0;
    uint32_t num_instances = 0;
    uint32_t index_offset = 0;
    int32_t vertex_offset = 0;
    uint32_t first_instance = 0;
};

// indirect compute dispatch command, as it is laid out in a GPU buffer
struct gpu_indirect_command_dispatch
{
    uint32_t dispatch_x = 0;
    uint32_t dispatch_y = 0;
    uint32_t dispatch_z = 0;
};

using resource_usage_flags_t = uint16_t;
struct resource_usage_flags
{
    enum : resource_usage_flags_t
    {
        none = 0,
        allow_uav = 1 << 0,
        allow_render_target = 1 << 1,
        allow_depth_stencil = 1 << 2,
        deny_shader_resource = 1 << 3,
        use_optimized_clear_value = 1 << 4,
    };
};

struct accel_struct_prebuild_info
{
    // the size in bytes of the backing buffers
    uint32_t buffer_size_bytes = 0;

    // the required scratch buffer size for the initial build
    uint32_t required_build_scratch_size_bytes = 0;

    // the required scratch buffer size for subsequent updates (requires accel_struct_build_flags::allow_update)
    uint32_t required_update_scratch_size_bytes = 0;
};

// flags to configure the building process of a raytracing acceleration structure
using accel_struct_build_flags_t = uint16_t;
struct accel_struct_build_flags
{
    enum : accel_struct_build_flags_t
    {
        none = 0,

        // build the AS so that it supports future updates
        allow_update = 1 << 0,

        // enable option to compact the AS
        // NOTE: compaction is not possible via PHI API
        allow_compaction = 1 << 1,

        // maximize raytracing performance at the cost of build time
        // typically 2-3 times longer
        // mutually exclusive with prefer_fast_build
        prefer_fast_trace = 1 << 2,

        // sacrifice raytracing performance for faster build time
        // typically 1/2 to 1/3 of the build time
        // mutually exclusive with prefer_fast_trace
        prefer_fast_build = 1 << 3,

        // minimize the scratch memory used and the result size
        // at the cost of build time and raytracing performance
        minimize_memory = 1 << 4,

        // do not create an internal scratch buffer
        // (PHI level, not native API)
        // if using this flag, you must supply a sufficiently large
        // scratch buffer in cmd::update_bottom_level/cmd::update_top_level
        no_internal_scratch_buffer = 1 << 5
    };
};

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

// bottom level accel struct instance within a top level accel struct (layout dictated by DXR/Vulkan RT Extension)
struct accel_struct_instance
{
    // Transposed transform matrix, containing only the top 3 rows (laid out as three 4-vectors)
    float transposed_transform[12];

    // Instance id - arbitrary value, accessed in shaders via `InstanceID()` (HLSL)
    uint32_t instance_id : 24;

    // Visibility mask - matched against `InstanceInclusionMask` parameter in `TraceRays(..)` (HLSL)
    uint32_t visibility_mask : 8;

    // Index of the hit group which will be invoked when a ray hits the instance
    uint32_t hit_group_index : 24;

    // Instance flags, such as culling
    accel_struct_instance_flags_t flags : 8;

    // Opaque handle of the bottom-level acceleration structure,
    // as received from `out_native_handle` in `createBottomLevelAccelStruct` (phi Backend)
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

// the sizes required for the four sections of a raytracing shader table
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

// info about current VRAM usage
struct vram_state_info
{
    // OS-provided VRAM budget in bytes, usage should stay below this
    uint64_t os_budget_bytes = 0;
    uint64_t current_usage_bytes = 0;
    uint64_t available_for_reservation_bytes = 0;
    uint64_t current_reservation_bytes = 0;
};

// info required to correlate CPU and GPU time measurements
struct clock_synchronization_info
{
    // the CPU clock is the one used by <clean-core/native/timing.hh>
    // on Win32: QueryPerformanceCounter/QueryPerformanceFrequency
    // on Linux: clock_gettime with CLOCK_MONOTONIC

    // amount of ticks of the CPU clock per second
    int64_t cpu_frequency = 0;
    // amount of ticks of the GPU clock per second
    int64_t gpu_frequency = 0;

    // CPU and GPU timestamps both representing the same real point in time
    int64_t cpu_reference_timestamp = 0;
    int64_t gpu_reference_timestamp = 0;

    // given a GPU timestamp (received via cmd::write_timestamp), returns
    // a representation comparable with regularly measured CPU timestamps
    int64_t convert_gpu_to_cpu(int64_t gpu_timestamp) const
    {
        return cpu_reference_timestamp + (gpu_timestamp - gpu_reference_timestamp) * cpu_frequency / gpu_frequency;
    }
};

// end of memhash structs
#ifdef CC_COMPILER_MSVC
#pragma warning(pop)
#endif
} // namespace phi
