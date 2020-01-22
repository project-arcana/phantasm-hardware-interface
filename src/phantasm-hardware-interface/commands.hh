#pragma once

#include <cstring>

#include <clean-core/utility.hh>

#include <typed-geometry/types/objects/aabb.hh>
#include <typed-geometry/types/size.hh>

#include "detail/trivial_capped_vector.hh"
#include "limits.hh"
#include "types.hh"

namespace phi
{
namespace cmd
{
namespace detail
{
#define PR_CMD_TYPE_VALUES        \
    PR_X(draw)                    \
    PR_X(dispatch)                \
    PR_X(transition_resources)    \
    PR_X(transition_image_slices) \
    PR_X(copy_buffer)             \
    PR_X(copy_texture)            \
    PR_X(copy_buffer_to_texture)  \
    PR_X(resolve_texture)         \
    PR_X(begin_render_pass)       \
    PR_X(end_render_pass)         \
    PR_X(debug_marker)            \
    PR_X(update_bottom_level)     \
    PR_X(update_top_level)        \
    PR_X(dispatch_rays)

enum class cmd_type : uint8_t
{
#define PR_X(_val_) _val_,
    PR_CMD_TYPE_VALUES
#undef PR_X
};

enum class cmd_queue_type : uint8_t
{
    none = 0x00,
    copy = 0x01,
    compute = 0x02,
    graphics = 0x04,
    all = copy | compute | graphics
};

struct cmd_base
{
    cmd_type s_internal_type;
    cmd_base(cmd_type t) : s_internal_type(t) {}
};

template <cmd_type TYPE, cmd_queue_type QUEUE = cmd_queue_type::graphics>
struct typed_cmd : cmd_base
{
    static constexpr cmd_queue_type s_queue_type = QUEUE;
    typed_cmd() : cmd_base(TYPE) {}
};

}

template <class T, uint8_t N>
using cmd_vector = phi::detail::trivial_capped_vector<T, N>;

#define PR_DEFINE_CMD(_type_) struct _type_ final : detail::typed_cmd<detail::cmd_type::_type_>

PR_DEFINE_CMD(begin_render_pass)
{
    struct render_target_info
    {
        shader_view_element sve;
        float clear_value[4];
        rt_clear_type clear_type;
    };

    struct depth_stencil_info
    {
        shader_view_element sve;
        float clear_value_depth;
        uint8_t clear_value_stencil;
        rt_clear_type clear_type;
    };

    cmd_vector<render_target_info, limits::max_render_targets> render_targets;
    depth_stencil_info depth_target;
    tg::isize2 viewport = tg::isize2(0, 0);

public:
    // convenience

    void add_backbuffer_rt(handle::resource res, bool clear = true)
    {
        render_targets.push_back(render_target_info{{}, {0.f, 0.f, 0.f, 1.f}, clear ? rt_clear_type::clear : rt_clear_type::load});
        render_targets.back().sve.init_as_backbuffer(res);
    }

    void add_2d_rt(handle::resource res, format pixel_format, rt_clear_type clear_op = rt_clear_type::clear, bool multisampled = false)
    {
        render_targets.push_back(render_target_info{{}, {0.f, 0.f, 0.f, 1.f}, clear_op});
        render_targets.back().sve.init_as_tex2d(res, pixel_format, multisampled);
    }

    void set_2d_depth_stencil(handle::resource res, format pixel_format, rt_clear_type clear_op = rt_clear_type::clear, bool multisampled = false)
    {
        depth_target = depth_stencil_info{{}, 1.f, 0, clear_op};
        depth_target.sve.init_as_tex2d(res, pixel_format, multisampled);
    }

    void set_null_depth_stencil() { depth_target.sve.init_as_null(); }
};

PR_DEFINE_CMD(end_render_pass){
    // NOTE: Anything useful to pass here?
};

PR_DEFINE_CMD(transition_resources)
{
    struct transition_info
    {
        handle::resource resource;
        resource_state target_state;
        shader_domain_flags_t dependant_shaders;
    };

    cmd_vector<transition_info, limits::max_resource_transitions> transitions;

public:
    // convenience

    /// add a barrier for resource [res] into new state [target]
    /// if the target state is a CBV/SRV/UAV, depending_shader must be
    /// the union of shaders depending upon this resource next (can be omitted on d3d12)
    void add(handle::resource res, resource_state target, shader_domain_flags_t depending_shader = {})
    {
        transitions.push_back(transition_info{res, target, depending_shader});
    }
};

PR_DEFINE_CMD(transition_image_slices)
{
    // Image slice transitions are entirely explicit, and require the user to synchronize before/after resource states

    struct slice_transition_info
    {
        handle::resource resource;
        resource_state source_state;
        resource_state target_state;
        shader_domain_flags_t source_dependencies;
        shader_domain_flags_t target_dependencies;
        int mip_level;
        int array_slice;
    };

    cmd_vector<slice_transition_info, limits::max_resource_transitions> transitions;

public:
    // convenience

    /// add a barrier for image [res] subresource at [mip_level] and [array_slice] from state [source] into new state [target]
    /// if the source/target state is a CBV/SRV/UAV, source/target_dep
    /// must be the union of shaders {previously accessing the resource (source) / depending upon this resource next (target)}
    /// (both can be omitted on d3d12)
    void add(handle::resource res, resource_state source, resource_state target, shader_domain_flags_t source_dep, shader_domain_flags_t target_dep,
             int level, int slice)
    {
        transitions.push_back(slice_transition_info{res, source, target, source_dep, target_dep, level, slice});
    }

    void add(handle::resource res, resource_state source, resource_state target, int level, int slice)
    {
        add(res, source, target, {}, {}, level, slice);
    }
};


PR_DEFINE_CMD(draw)
{
    static_assert(limits::max_root_constant_bytes > 0, "root constant size must be nonzero");

    std::byte root_constants[limits::max_root_constant_bytes];
    cmd_vector<shader_argument, limits::max_shader_arguments> shader_arguments;
    handle::pipeline_state pipeline_state = handle::null_pipeline_state;
    handle::resource vertex_buffer = handle::null_resource;
    handle::resource index_buffer = handle::null_resource;
    unsigned num_indices = 0;
    unsigned index_offset = 0;
    unsigned vertex_offset = 0;

    /// the scissor rectangle to set, none if -1
    /// left, top, right, bottom of the rectangle in absolute pixel values
    tg::iaabb2 scissor = tg::iaabb2(-1, -1);

public:
    // convenience

    void init(handle::pipeline_state pso, unsigned num_ind, handle::resource vb = handle::null_resource, handle::resource ib = handle::null_resource,
              unsigned ind_offset = 0, unsigned vert_offset = 0)
    {
        pipeline_state = pso;
        num_indices = num_ind;
        vertex_buffer = vb;
        index_buffer = ib;
        index_offset = ind_offset;
        vertex_offset = vert_offset;
    }

    void add_shader_arg(handle::resource cbv, unsigned cbv_off = 0, handle::shader_view sv = handle::null_shader_view)
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

PR_DEFINE_CMD(dispatch)
{
    static_assert(limits::max_root_constant_bytes > 0, "root constant size must be nonzero");

    std::byte root_constants[limits::max_root_constant_bytes];
    cmd_vector<shader_argument, limits::max_shader_arguments> shader_arguments;
    unsigned dispatch_x = 0;
    unsigned dispatch_y = 0;
    unsigned dispatch_z = 0;
    handle::pipeline_state pipeline_state = handle::null_pipeline_state;

public:
    // convenience

    void init(handle::pipeline_state pso, unsigned x, unsigned y, unsigned z)
    {
        pipeline_state = pso;
        dispatch_x = x;
        dispatch_y = y;
        dispatch_z = z;
    }

    void add_shader_arg(handle::resource cbv, unsigned cbv_off = 0, handle::shader_view sv = handle::null_shader_view)
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

        //        if constexpr (sizeof(T) < sizeof(root_constants))
        //        {
        //            std::memset(root_constants + sizeof(T), 0, sizeof(root_constants) - sizeof(T));
        //        }
    }
};

PR_DEFINE_CMD(copy_buffer)
{
    handle::resource source;
    handle::resource destination;
    size_t dest_offset;
    size_t source_offset;
    size_t size;

public:
    // convenience

    copy_buffer() = default;
    copy_buffer(handle::resource dest, size_t dest_offset, handle::resource src, size_t src_offset, size_t size)
      : source(src), destination(dest), dest_offset(dest_offset), source_offset(src_offset), size(size)
    {
    }

    void init(handle::resource src, handle::resource dest, size_t size, size_t src_offset = 0, size_t dst_offset = 0)
    {
        source = src;
        destination = dest;
        this->size = size;
        source_offset = src_offset;
        dest_offset = dst_offset;
    }
};

PR_DEFINE_CMD(copy_texture)
{
    handle::resource source;
    handle::resource destination;
    unsigned src_mip_index;    ///< index of the MIP level to read from
    unsigned src_array_index;  ///< index of the first array element to read from (usually: 0)
    unsigned dest_mip_index;   ///< index of the MIP level to write to
    unsigned dest_array_index; ///< index of the first array element to write to (usually: 0)
    unsigned width;            ///< width of the destination texture (in the specified MIP map and array element(s))
    unsigned height;           ///< height of the destination texture (in the specified MIP map and array element(s))
    unsigned num_array_slices; ///< amount of array slices to copy, all other parameters staying equal (usually: 1)

public:
    // convenience

    void init_symmetric(handle::resource src, handle::resource dest, unsigned width, unsigned height, unsigned mip_index,
                        unsigned first_array_index = 0, unsigned num_array_slices = 1)
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

PR_DEFINE_CMD(copy_buffer_to_texture)
{
    handle::resource source;
    handle::resource destination;
    size_t source_offset;
    unsigned dest_width;       ///< width of the destination texture (in the specified MIP map and array element)
    unsigned dest_height;      ///< height of the destination texture (in the specified MIP map and array element)
    unsigned dest_mip_index;   ///< index of the MIP level to copy
    unsigned dest_array_index; ///< index of the array element to copy (usually: 0)

public:
    void init(handle::resource src, handle::resource dest, unsigned dest_w, unsigned dest_h, size_t src_offset = 0, unsigned dest_mip_i = 0, unsigned dest_arr_i = 0)
    {
        source = src;
        destination = dest;
        source_offset = src_offset;
        dest_width = dest_w;
        dest_height = dest_h;
        dest_mip_index = dest_mip_i;
        dest_array_index = dest_arr_i;
    }
};

PR_DEFINE_CMD(resolve_texture)
{
    handle::resource source;
    handle::resource destination;
    unsigned src_mip_index;    ///< index of the MIP level to read from (usually: 0)
    unsigned src_array_index;  ///< index of the array element to read from (usually: 0)
    unsigned dest_mip_index;   ///< index of the MIP level to write to (usually: 0)
    unsigned dest_array_index; ///< index of the array element to write to (usually: 0)
    unsigned width;            ///< width of the destination texture (in the specified MIP map and array element) (ignored on d3d12)
    unsigned height;           ///< height of the destination texture (in the specified MIP map and array element) (ignored on d3d12)

public:
    // convenience

    void init_symmetric(handle::resource src, handle::resource dest, unsigned width, unsigned height, unsigned mip_index = 0, unsigned array_index = 0)
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

PR_DEFINE_CMD(debug_marker)
{
    char const* string_literal;

    debug_marker() = default;
    debug_marker(char const* s) : string_literal(s) {}
};

PR_DEFINE_CMD(update_bottom_level)
{
    handle::accel_struct dest = handle::null_accel_struct;
    handle::accel_struct source = handle::null_accel_struct;
};

PR_DEFINE_CMD(update_top_level)
{
    handle::accel_struct dest = handle::null_accel_struct;
    unsigned num_instances = 0;
};

PR_DEFINE_CMD(dispatch_rays)
{
    handle::pipeline_state pso = handle::null_pipeline_state;
    handle::resource table_raygen = handle::null_resource;
    handle::resource table_miss = handle::null_resource;
    handle::resource table_hitgroups = handle::null_resource;
    unsigned width = 0;
    unsigned height = 0;
    unsigned depth = 0;
};

#undef PR_DEFINE_CMD

namespace detail
{
#define PR_X(_val_)                                                                                                                       \
    static_assert(std::is_trivially_copyable_v<::phi::cmd::_val_> && std::is_trivially_destructible_v<::phi::cmd::_val_>, \
                  #_val_ " is not trivially copyable / destructible");
PR_CMD_TYPE_VALUES
#undef PR_X

    /// returns the size in bytes of the given command
    [[nodiscard]] inline constexpr size_t
    get_command_size(detail::cmd_type type)
{
    switch (type)
    {
#define PR_X(_val_)               \
    case detail::cmd_type::_val_: \
        return sizeof(::phi::cmd::_val_);
        PR_CMD_TYPE_VALUES
#undef PR_X
    }
    return 0; // suppress warnings
}

/// returns a string literal corresponding to the command type
[[nodiscard]] inline constexpr char const* to_string(detail::cmd_type type)
{
    switch (type)
    {
#define PR_X(_val_)               \
    case detail::cmd_type::_val_: \
        return #_val_;
        PR_CMD_TYPE_VALUES
#undef PR_X
    }
    return ""; // suppress warnings
}

/// calls F::execute() with the apropriately downcasted command object as a const&
/// (F should have an execute method with overloads for all command objects)
template <class F>
void dynamic_dispatch(detail::cmd_base const& base, F& callback)
{
    switch (base.s_internal_type)
    {
#define PR_X(_val_)                                                            \
    case detail::cmd_type::_val_:                                              \
        callback.execute(static_cast<::phi::cmd::_val_ const&>(base)); \
        break;
        PR_CMD_TYPE_VALUES
#undef PR_X
    }
}

[[nodiscard]] inline constexpr size_t compute_max_command_size()
{
    size_t res = 0;
#define PR_X(_val_) res = cc::max(res, sizeof(::phi::cmd::_val_));
    PR_CMD_TYPE_VALUES
#undef PR_X
    return res;
}

inline constexpr size_t max_command_size = compute_max_command_size();
}

#undef PR_CMD_TYPE_VALUES
}

struct command_stream_parser
{
public:
    struct iterator_end
    {
    };

    struct iterator
    {
        iterator(std::byte* pos, size_t size)
          : _pos(reinterpret_cast<cmd::detail::cmd_base*>(pos)), _remaining_size(_pos == nullptr ? 0 : static_cast<int64_t>(size))
        {
        }

        bool operator!=(iterator_end) const noexcept { return _remaining_size > 0; }

        iterator& operator++()
        {
            auto const advance = cmd::detail::get_command_size(_pos->s_internal_type);
            _pos = reinterpret_cast<cmd::detail::cmd_base*>(reinterpret_cast<std::byte*>(_pos) + advance);
            _remaining_size -= advance;
            return *this;
        }

        cmd::detail::cmd_base& operator*() const { return *(_pos); }

    private:
        cmd::detail::cmd_base* _pos = nullptr;
        int64_t _remaining_size = 0;
    };

public:
    command_stream_parser() = default;
    command_stream_parser(std::byte* buffer, size_t size) : _in_buffer(buffer), _size(buffer == nullptr ? 0 : size) {}

    void set_buffer(std::byte* buffer, size_t size)
    {
        _in_buffer = buffer;
        _size = (buffer == nullptr ? 0 : size);
    }

    iterator begin() const { return iterator(_in_buffer, _size); }
    iterator_end end() const { return iterator_end(); }

private:
    std::byte* _in_buffer = nullptr;
    size_t _size = 0;
};

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

#ifndef PR_ENABLE_DEBUG_MARKERS
    void add_command(cmd::debug_marker const&)
    {
        // no-op
    }
#endif

public:
    size_t size() const { return _cursor; }
    std::byte* buffer() const { return _out_buffer; }

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
