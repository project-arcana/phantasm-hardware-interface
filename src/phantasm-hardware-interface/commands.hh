#pragma once

#include <cstring>

#include <typed-geometry/types/objects/aabb.hh>
#include <typed-geometry/types/size.hh>

#include <phantasm-hardware-interface/common/command_base.hh>
#include <phantasm-hardware-interface/common/container/flat_vector.hh>
#include <phantasm-hardware-interface/limits.hh>
#include <phantasm-hardware-interface/types.hh>

#ifdef PHI_HAS_OPTICK
namespace Optick
{
struct EventDescription;
}

// create an Optick event name for use with cmd::begin_debug_label
#define PHI_CREATE_OPTICK_EVENT(VariableName, NameString)                                  \
    static ::Optick::EventDescription* VariableName = nullptr;                             \
    if (VariableName == nullptr)                                                           \
    {                                                                                      \
        VariableName = ::Optick::EventDescription::Create(NameString, __FILE__, __LINE__); \
    }                                                                                      \
    CC_FORCE_SEMICOLON
#endif

namespace phi
{
namespace cmd
{
PHI_DEFINE_CMD(begin_render_pass)
{
    // Start a render pass into the given render targets

    // draw calls are only possibly inside a render pass
    // ends with cmd::end_render_pass

    struct render_target_info
    {
        resource_view rv;
        float clear_value[4];
        rt_clear_type clear_type;
    };

    struct depth_stencil_info
    {
        resource_view rv = resource_view::null();
        float clear_value_depth;
        uint8_t clear_value_stencil;
        rt_clear_type clear_type;
    };

    flat_vector<render_target_info, limits::max_render_targets> render_targets;
    depth_stencil_info depth_target;
    tg::isize2 viewport = {0, 0};       ///< viewport dimensions being rendered to, in pixels
    tg::ivec2 viewport_offset = {0, 0}; ///< offset of the viewport, in pixels from the top left corner

public:
    void add_backbuffer_rt(handle::resource res, bool clear = true)
    {
        render_targets.push_back(render_target_info{{}, {0.f, 0.f, 0.f, 1.f}, clear ? rt_clear_type::clear : rt_clear_type::load});
        render_targets.back().rv.init_as_backbuffer(res);
    }

    void add_2d_rt(handle::resource res, format pixel_format, rt_clear_type clear_op = rt_clear_type::clear, bool multisampled = false)
    {
        render_targets.push_back(render_target_info{{}, {0.f, 0.f, 0.f, 1.f}, clear_op});
        render_targets.back().rv.init_as_tex2d(res, pixel_format, multisampled);
    }

    void set_2d_depth_stencil(handle::resource res, format pixel_format, rt_clear_type clear_op = rt_clear_type::clear, bool multisampled = false)
    {
        depth_target = depth_stencil_info{{}, 1.f, 0, clear_op};
        depth_target.rv.init_as_tex2d(res, pixel_format, multisampled);
    }

    void set_null_depth_stencil() { depth_target.rv.init_as_null(); }
};

PHI_DEFINE_CMD(end_render_pass){
    // End a render pass previously begun with cmd::begin_render_pass
};

PHI_DEFINE_CMD(transition_resources)
{
    // Transition resources to a new state

    // Resource state transitions are simplified - only the after-state is given
    // the before state is internally managed, and submit-order-safe
    // NOTE: the first transition of each resource in a commandlist is implicit,
    // it is inserted last-minute at submission - thus, that resource is in that state
    // not just after the transition, but right away from the beginning of the cmdlist

    flat_vector<transition_info, limits::max_resource_transitions> transitions;

public:
    /// add a barrier for resource [res] into new state [target]
    /// if the target state is a CBV/SRV/UAV, depending_shader must be
    /// the union of shaders depending upon this resource next (can be omitted on d3d12)
    void add(handle::resource res, resource_state target, shader_stage_flags_t depending_shader = {})
    {
        transitions.push_back(transition_info{res, target, depending_shader});
    }
};

PHI_DEFINE_CMD(transition_image_slices)
{
    // Image slice transitions are entirely explicit, and require the user to synchronize before/after resource states

    struct slice_transition_info
    {
        handle::resource resource;
        resource_state source_state;
        resource_state target_state;
        shader_stage_flags_t source_dependencies;
        shader_stage_flags_t target_dependencies;
        int mip_level;
        int array_slice;
    };

    flat_vector<slice_transition_info, limits::max_resource_transitions> transitions;

public:
    /// add a barrier for image [res] subresource at [mip_level] and [array_slice] from state [source] into new state [target]
    /// if the source/target state is a CBV/SRV/UAV, source/target_dep
    /// must be the union of shaders {previously accessing the resource (source) / depending upon this resource next (target)}
    /// (both can be omitted on d3d12)
    void add(handle::resource res, resource_state source, resource_state target, shader_stage_flags_t source_dep, shader_stage_flags_t target_dep,
             int level, int slice)
    {
        transitions.push_back(slice_transition_info{res, source, target, source_dep, target_dep, level, slice});
    }

    void add(handle::resource res, resource_state source, resource_state target, int level, int slice)
    {
        add(res, source, target, {}, {}, level, slice);
    }
};

PHI_DEFINE_CMD(barrier_uav)
{
    // Explicitly record UAV barriers on the spot, no tracking

    // If no resources are given, a full UAV barrier is issued

    flat_vector<handle::resource, limits::max_uav_barriers> resources; // optional
};


PHI_DEFINE_CMD(draw)
{
    // Execute a draw call

    // must occur inside of a render pass

    std::byte root_constants[limits::max_root_constant_bytes];                   // optional
    flat_vector<shader_argument, limits::max_shader_arguments> shader_arguments; // optional
    handle::pipeline_state pipeline_state = handle::null_pipeline_state;
    handle::resource vertex_buffer = handle::null_resource; // optional
    handle::resource index_buffer = handle::null_resource;  // optional

    /// amount of instances to draw
    uint32_t num_instances = 1;
    /// amount of indices drawn (or amount of vertices if no index buffer specified)
    uint32_t num_indices = 0;
    /// location of the first index (or first vertex if no index buffer specified)
    uint32_t index_offset = 0;
    /// added to the vertex index before indexing into the vertex buffer
    int vertex_offset = 0;

    /// the scissor rectangle to set, none if -1
    /// left, top, right, bottom of the rectangle in absolute pixel values
    tg::iaabb2 scissor = tg::iaabb2(-1, -1);

public:
    void init(handle::pipeline_state pso, uint32_t num_ind, handle::resource vb = handle::null_resource, handle::resource ib = handle::null_resource,
              uint32_t ind_offset = 0, int vert_offset = 0)
    {
        pipeline_state = pso;
        num_indices = num_ind;
        vertex_buffer = vb;
        index_buffer = ib;
        index_offset = ind_offset;
        vertex_offset = vert_offset;
    }

    void add_shader_arg(handle::resource cbv, uint32_t cbv_off = 0, handle::shader_view sv = handle::null_shader_view)
    {
        shader_arguments.push_back(shader_argument{cbv, sv, cbv_off});
    }

    template <class T>
    void write_root_constants(T const& data)
    {
        static_assert(sizeof(T) <= sizeof(root_constants), "data too large");
        static_assert(std::is_trivially_copyable_v<T>, "data not memcpyable");
        static_assert(!std::is_pointer_v<T>, "provide direct reference to data");
        std::memcpy(root_constants, &data, sizeof(T));
    }

    void set_scissor(int left, int top, int right, int bot) { scissor = tg::iaabb2({left, top}, {right, bot}); }
};

PHI_DEFINE_CMD(draw_indirect)
{
    // Execute draw calls based on data in a GPU buffer

    // vertex/index ranges are specified by an array of structs in indirect_argument_buffer
    // if an index buffer is provided, the arg type is phi::gpu_indirect_command_draw_indexed
    // otherwise, the arg type is phi::gpu_indirect_command_draw
    // num_arguments specifies the amount of these arguments to read from indirect_argument_buffer
    // indirect_argument_buffer must be in state indirect_argument

    std::byte root_constants[limits::max_root_constant_bytes];
    flat_vector<shader_argument, limits::max_shader_arguments> shader_arguments;
    handle::pipeline_state pipeline_state = handle::null_pipeline_state;

    /// the buffer from which to read arguments, must be in resource_state::indirect_argument
    handle::resource indirect_argument_buffer = handle::null_resource;
    uint32_t argument_buffer_offset_bytes = 0; ///< offset in bytes into the argument buffer
    uint32_t num_arguments = 0;                ///< amount of arguments to read from the buffer

    handle::resource vertex_buffer = handle::null_resource; ///< optional
    handle::resource index_buffer = handle::null_resource;  ///< optional

public:
    void add_shader_arg(handle::resource cbv, uint32_t cbv_off = 0, handle::shader_view sv = handle::null_shader_view)
    {
        shader_arguments.push_back(shader_argument{cbv, sv, cbv_off});
    }

    template <class T>
    void write_root_constants(T const& data)
    {
        static_assert(sizeof(T) <= sizeof(root_constants), "data too large");
        static_assert(std::is_trivially_copyable_v<T>, "data not memcpyable");
        static_assert(!std::is_pointer_v<T>, "provide direct reference to data");
        std::memcpy(root_constants, &data, sizeof(T));
    }
};

PHI_DEFINE_CMD(dispatch)
{
    // Execute a compute dispatch

    std::byte root_constants[limits::max_root_constant_bytes];
    flat_vector<shader_argument, limits::max_shader_arguments> shader_arguments;
    handle::pipeline_state pipeline_state = handle::null_pipeline_state;

    uint32_t dispatch_x = 0;
    uint32_t dispatch_y = 0;
    uint32_t dispatch_z = 0;

public:
    void init(handle::pipeline_state pso, uint32_t x, uint32_t y = 1, uint32_t z = 1)
    {
        pipeline_state = pso;
        dispatch_x = x;
        dispatch_y = y;
        dispatch_z = z;
    }

    void add_shader_arg(handle::resource cbv, uint32_t cbv_off = 0, handle::shader_view sv = handle::null_shader_view)
    {
        shader_arguments.push_back(shader_argument{cbv, sv, cbv_off});
    }

    template <class T>
    void write_root_constants(T const& data)
    {
        static_assert(sizeof(T) <= sizeof(root_constants), "data too large");
        static_assert(std::is_trivially_copyable_v<T>, "data not memcpyable");
        static_assert(!std::is_pointer_v<T>, "provide direct reference to data");
        std::memcpy(root_constants, &data, sizeof(T));
    }
};

PHI_DEFINE_CMD(dispatch_indirect)
{
    // Execute a compute dispatch

    std::byte root_constants[limits::max_root_constant_bytes];
    flat_vector<shader_argument, limits::max_shader_arguments> shader_arguments;
    handle::pipeline_state pipeline_state = handle::null_pipeline_state;

    /// the buffer location to read arguments from, must be in resource_state::indirect_argument
    buffer_address argument_buffer_addr;
    /// the amount of arguments to read from the buffer
    uint32_t num_arguments = 0;

public:
    void add_shader_arg(handle::resource cbv, uint32_t cbv_off = 0, handle::shader_view sv = handle::null_shader_view)
    {
        shader_arguments.push_back(shader_argument{cbv, sv, cbv_off});
    }

    template <class T>
    void write_root_constants(T const& data)
    {
        static_assert(sizeof(T) <= sizeof(root_constants), "data too large");
        static_assert(std::is_trivially_copyable_v<T>, "data not memcpyable");
        static_assert(!std::is_pointer_v<T>, "provide direct reference to data");
        std::memcpy(root_constants, &data, sizeof(T));
    }
};

PHI_DEFINE_CMD(copy_buffer)
{
    // Copy data between buffers

    handle::resource source = handle::null_resource;
    handle::resource destination = handle::null_resource;
    size_t dest_offset_bytes = 0;
    size_t source_offset_bytes = 0;
    size_t size = 0;

public:
    copy_buffer() = default;
    copy_buffer(handle::resource dest, size_t dest_offset, handle::resource src, size_t src_offset, size_t size)
      : source(src), destination(dest), dest_offset_bytes(dest_offset), source_offset_bytes(src_offset), size(size)
    {
    }

    void init(handle::resource src, handle::resource dest, size_t size, size_t src_offset = 0, size_t dst_offset = 0)
    {
        source = src;
        destination = dest;
        this->size = size;
        source_offset_bytes = src_offset;
        dest_offset_bytes = dst_offset;
    }
};

PHI_DEFINE_CMD(copy_texture)
{
    // Copy data between textures

    handle::resource source = handle::null_resource;
    handle::resource destination = handle::null_resource;
    uint32_t src_mip_index = 0;    ///< index of the MIP level to read from
    uint32_t src_array_index = 0;  ///< index of the first array element to read from (usually: 0)
    uint32_t dest_mip_index = 0;   ///< index of the MIP level to write to
    uint32_t dest_array_index = 0; ///< index of the first array element to write to (usually: 0)
    uint32_t width = 0;            ///< width of the destination texture (in the specified MIP map and array element(s))
    uint32_t height = 0;           ///< height of the destination texture (in the specified MIP map and array element(s))
    uint32_t num_array_slices = 0; ///< amount of array slices to copy, all other parameters staying equal (usually: 1)

public:
    void init_symmetric(handle::resource src, handle::resource dest, uint32_t width, uint32_t height, uint32_t mip_index,
                        uint32_t first_array_index = 0, uint32_t num_array_slices = 1)
    {
        source = src;
        destination = dest;
        this->width = width;
        this->height = height;
        src_mip_index = mip_index;
        dest_mip_index = mip_index;
        src_array_index = first_array_index;
        dest_array_index = first_array_index;
        this->num_array_slices = num_array_slices;
    }
};

PHI_DEFINE_CMD(copy_buffer_to_texture)
{
    // Copy data from a buffer to a texture

    handle::resource source = handle::null_resource;
    handle::resource destination = handle::null_resource;
    size_t source_offset_bytes = 0;
    uint32_t dest_width = 0;       ///< width of the destination texture (in the specified MIP level and array element)
    uint32_t dest_height = 0;      ///< height of the destination texture (in the specified MIP level and array element)
    uint32_t dest_mip_index = 0;   ///< index of the MIP level to copy
    uint32_t dest_array_index = 0; ///< index of the array element to copy (usually: 0)

public:
    void init(handle::resource src, handle::resource dest, uint32_t dest_w, uint32_t dest_h, size_t src_offset = 0, uint32_t dest_mip_i = 0, uint32_t dest_arr_i = 0)
    {
        source = src;
        destination = dest;
        source_offset_bytes = src_offset;
        dest_width = dest_w;
        dest_height = dest_h;
        dest_mip_index = dest_mip_i;
        dest_array_index = dest_arr_i;
    }
};

PHI_DEFINE_CMD(copy_texture_to_buffer)
{
    // Copy data from a texture to a buffer

    handle::resource source = handle::null_resource;
    handle::resource destination = handle::null_resource;
    size_t dest_offset = 0;
    uint32_t src_width = 0;       ///< width of the source texture (in the specified MIP level and array element)
    uint32_t src_height = 0;      ///< height of the destination texture (in the specified MIP level and array element)
    uint32_t src_mip_index = 0;   ///< index of the MIP level to copy
    uint32_t src_array_index = 0; ///< index of the array element to copy (usually: 0)

public:
    void init(handle::resource src, handle::resource dest, uint32_t src_w, uint32_t src_h, size_t dest_off = 0, uint32_t src_mip_i = 0, uint32_t src_arr_i = 0)
    {
        source = src;
        destination = dest;
        dest_offset = dest_off;
        src_width = src_w;
        src_height = src_h;
        src_mip_index = src_mip_i;
        src_array_index = src_arr_i;
    }
};

PHI_DEFINE_CMD(resolve_texture)
{
    // Resolve MSAA render targets or textures

    handle::resource source = handle::null_resource;      ///< the multisampled source texture
    handle::resource destination = handle::null_resource; ///< the non-multisampled destination texture
    uint32_t src_mip_index = 0;                           ///< index of the MIP level to read from (usually: 0)
    uint32_t src_array_index = 0;                         ///< index of the array element to read from (usually: 0)
    uint32_t dest_mip_index = 0;                          ///< index of the MIP level to write to (usually: 0)
    uint32_t dest_array_index = 0;                        ///< index of the array element to write to (usually: 0)
    uint32_t width = 0;  ///< width of the destination texture (in the specified MIP map and array element) (ignored on d3d12)
    uint32_t height = 0; ///< height of the destination texture (in the specified MIP map and array element) (ignored on d3d12)

public:
    void init_symmetric(handle::resource src, handle::resource dest, uint32_t width, uint32_t height, uint32_t mip_index = 0, uint32_t array_index = 0)
    {
        source = src;
        destination = dest;
        this->width = width;
        this->height = height;
        src_mip_index = mip_index;
        dest_mip_index = mip_index;
        src_array_index = array_index;
        dest_array_index = array_index;
    }
};

PHI_DEFINE_CMD(write_timestamp)
{
    // Write the current GPU queue timestamp into a slot of a query range

    // see cmd::resolve_queries to receive the data afterwards

    handle::query_range query_range = handle::null_query_range; ///< the query_range in which to write a timestamp query
    uint32_t index = 0;                                         ///< relative index into the query_range, element to write to

public:
    write_timestamp() = default;
    write_timestamp(handle::query_range qr, uint32_t index = 0) : query_range(qr), index(index) {}
};

PHI_DEFINE_CMD(resolve_queries)
{
    // Write a uint64 value per query in a given query range to a buffer

    // this allows usage of the data contained in queries
    // typically dest_buffer would be a readback buffer
    // to interpret timestamp results, see get_timestamp_difference_microseconds in util.hh

    handle::resource dest_buffer = handle::null_resource;           ///< the buffer in which to write the resolve data
    handle::query_range src_query_range = handle::null_query_range; ///< the query_range from which to read
    uint32_t query_start = 0;                                       ///< relative index into the query_range, element to start the resolve from
    uint32_t num_queries = 1;                                       ///< amount of elements to resolve
    uint32_t dest_offset_bytes = 0;                                 ///< offset into the destination buffer

public:
    void init(handle::resource dest, handle::query_range qr, uint32_t start = 0, uint32_t num = 1, uint32_t dest_offset = 0)
    {
        dest_buffer = dest;
        src_query_range = qr;
        query_start = start;
        num_queries = num;
        this->dest_offset_bytes = dest_offset;
    }
};

PHI_DEFINE_CMD(begin_debug_label)
{
    // Begin a debug label on the cmdlist
    // close it using cmd::end_debug_label
    // for diagnostic tools like renderdoc, nsight, gpa, pix

    char const* string = "UNLABELED_DEBUG_MARKER";

public:
    begin_debug_label() = default;
    begin_debug_label(char const* s) : string(s) {}
};

PHI_DEFINE_CMD(end_debug_label){
    // Close a debug label started with cmd::begin_debug_label
};

PHI_DEFINE_CMD(begin_profile_scope)
{
    // Creates a GPU profile scope on the cmdlist
    // close it using cmd::end_profile_scope
    // usage depends on enabled profilers, see CMake options

#ifdef PHI_HAS_OPTICK
    // point to a manually allocated Optick EventDescription
    // create one using PHI_CREATE_OPTICK_EVENT(VariableName, NameString)
    Optick::EventDescription* optick_event = nullptr;
#endif
};

PHI_DEFINE_CMD(end_profile_scope){
    // Close a profile scope started with cmd::begin_profile_scope
};

PHI_DEFINE_CMD(update_bottom_level)
{
    // Update or build a bottom level raytracing acceleration structure (BLAS)

    /// the bottom level accel struct to build
    handle::accel_struct dest = handle::null_accel_struct;

    /// the bottom level accel struct to update from (optional)
    /// if specified, dest must have been created with accel_struct_build_flags::allow_update
    /// can be the same as dest for an in-place update
    handle::accel_struct source = handle::null_accel_struct;
};

PHI_DEFINE_CMD(update_top_level)
{
    // Update or build a top level raytracing acceleration structure (TLAS),
    // filling it with instances of bottom level accel structs (BLAS)

    /// amount of instances to write
    uint32_t num_instances = 0;

    /// a buffer holding an array of accel_struct_instance structs (at least num_instances)
    buffer_address source_instances_addr;

    /// the top level accel struct to update
    handle::accel_struct dest_accel_struct = handle::null_accel_struct;
};


PHI_DEFINE_CMD(dispatch_rays)
{
    // Dispatch rays given a raytracing pipeline state and shader tables for ray generation, ray miss and the involved hitgroups

    handle::pipeline_state pso = handle::null_pipeline_state;

    buffer_range table_ray_generation;
    buffer_range_and_stride table_miss;
    buffer_range_and_stride table_hit_groups;
    buffer_range_and_stride table_callable; ///< optional

    uint32_t dispatch_x = 1;
    uint32_t dispatch_y = 1;
    uint32_t dispatch_z = 1;

public:
    void set_strides(shader_table_strides const& strides)
    {
        table_ray_generation.size_bytes = strides.size_ray_gen;

        table_miss.stride_bytes = strides.stride_miss;
        table_miss.size_bytes = strides.size_miss;

        table_hit_groups.stride_bytes = strides.stride_hit_group;
        table_hit_groups.size_bytes = strides.size_hit_group;

        table_callable.stride_bytes = strides.stride_callable;
        table_callable.size_bytes = strides.size_callable;
    }

    void set_single_buffer(handle::resource shader_table, bool include_callable)
    {
        table_ray_generation.buffer = shader_table;
        table_miss.buffer = shader_table;
        table_hit_groups.buffer = shader_table;

        if (include_callable)
            table_callable.buffer = shader_table;
    }

    void set_offsets(uint32_t offset_ray_gen, uint32_t offset_miss, uint32_t offset_hit_group, uint32_t offset_callable)
    {
        table_ray_generation.offset_bytes = offset_ray_gen;
        table_miss.offset_bytes = offset_miss;
        table_hit_groups.offset_bytes = offset_hit_group;
        table_callable.offset_bytes = offset_callable;
    }

    [[deprecated("debug only")]] void set_zero_sizes()
    {
        // NOTE: sizes are functionally irrelevant, setting them to 0 has no effects for D3D12, even with GBV.
        // Vulkan doesn't even have an option to specify them
        // However according to Adam Miles, they might be used by some tooling (like PIX)
        // of course, we can validate based on the sizes as well
        table_ray_generation.size_bytes = 0;
        table_miss.size_bytes = 0;
        table_hit_groups.size_bytes = 0;
        table_callable.size_bytes = 0;
    }
};

PHI_DEFINE_CMD(clear_textures)
{
    // Clear up to 4 textures to specified values - standalone (outside of begin/end_render_pass)

    struct clear_info
    {
        resource_view rv;
        rt_clear_value value;
    };

    flat_vector<clear_info, 4> clear_ops;
};

PHI_DEFINE_CMD(code_location_marker)
{
    // Mark the code location that currently writes commands, can be viewed in command list translators
    // no GPU / API effects whatsoever

    char const* function = "NO_DEBUG_LOCATION";
    char const* file = "NO_DEBUG_LOCATION";
    int line = 0;

    code_location_marker(char const* func, char const* file, int line) : function(func), file(file), line(line) {}
};

#define PHI_CMD_CODE_LOCATION() \
    ::phi::cmd::code_location_marker { __FUNCTION__, __FILE__, __LINE__ }

#undef PHI_DEFINE_CMD
}

struct command_stream_writer
{
public:
    command_stream_writer() = default;
    command_stream_writer(std::byte* buffer, size_t size) : _out_buffer(buffer), _max_size(size), _cursor(0) {}

    void initialize(std::byte* buffer, size_t size)
    {
        _out_buffer = buffer;
        _max_size = size;
        _cursor = 0;
    }

    /// exchange the underlying buffer without resetting the cursor
    void exchange_buffer(std::byte* new_buffer, size_t new_size)
    {
        _out_buffer = new_buffer;
        _max_size = new_size;
    }

    void reset() { _cursor = 0; }

    template <class CMDT>
    void add_command(CMDT const& command)
    {
        static_assert(std::is_base_of_v<cmd::detail::cmd_base, CMDT>, "not a command");
        static_assert(std::is_trivially_copyable_v<CMDT>, "command not trivially copyable");
        CC_ASSERT(can_accomodate_t<CMDT>() && "command_stream_writer full");
        new (cc::placement_new, _out_buffer + _cursor) CMDT(command);
        _cursor += sizeof(CMDT);
    }

    template <class CMDT>
    [[nodiscard]] CMDT& emplace_command()
    {
        static_assert(std::is_base_of_v<cmd::detail::cmd_base, CMDT>, "not a command");
        static_assert(std::is_trivially_copyable_v<CMDT>, "command not trivially copyable");
        CC_ASSERT(can_accomodate_t<CMDT>() && "command_stream_writer full");
        CMDT* const res = new (cc::placement_new, _out_buffer + _cursor) CMDT();
        _cursor += sizeof(CMDT);
        return *res;
    }

    void advance_cursor(size_t amount) { _cursor += amount; }

public:
    /// returns the size of the written section in bytes
    size_t size() const { return _cursor; }
    /// returns the start of the buffer
    std::byte* buffer() const { return _out_buffer; }
    /// returns the current head of the buffer
    std::byte* buffer_head() const { return _out_buffer + _cursor; }
    /// returns the maximum size of the buffer
    size_t max_size() const { return _max_size; }

    bool empty() const { return _cursor == 0; }

    int remaining_bytes() const { return static_cast<int>(_max_size) - static_cast<int>(_cursor); }

    template <class CMDT>
    bool can_accomodate_t() const
    {
        static_assert(std::is_base_of_v<cmd::detail::cmd_base, CMDT>, "not a command");
        return static_cast<int>(sizeof(CMDT)) <= remaining_bytes();
    }

    bool can_accomodate(size_t size) const { return static_cast<int>(size) <= remaining_bytes(); }

private:
    std::byte* _out_buffer = nullptr;
    size_t _max_size = 0;
    size_t _cursor = 0;
};
}
