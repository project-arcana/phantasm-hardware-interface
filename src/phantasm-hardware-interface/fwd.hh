#pragma once

#include <cstdint>

#include <clean-core/fwd.hh>

namespace phi
{
// backends
class Backend;

// config structs
struct backend_config;
struct window_handle;
struct gpu_info;

// config enums
enum class backend_type : uint8_t;
enum class adapter_preference : uint8_t;
enum class validation_level : uint8_t;

// data structs
struct shader_argument;
struct transition_info;
struct resource_view;
struct vertex_attribute_info;
struct sampler_config;
struct pipeline_config;
struct render_target_config;
struct rt_clear_value;
struct blend_state;
struct gpu_indirect_command_draw;
struct gpu_indirect_command_draw_indexed;
struct accel_struct_instance;
struct shader_table_strides;

// data enums
enum class format : uint8_t;
enum class shader_stage : uint8_t;
enum class queue_type : uint8_t;
enum class present_mode : uint8_t;
enum class resource_state : uint8_t;
enum class resource_heap : uint8_t;
enum class texture_dimension : uint8_t;
enum class resource_view_dimension : uint8_t;
enum class sampler_filter : uint8_t;
enum class sampler_address_mode : uint8_t;
enum class sampler_compare_func : uint8_t;
enum class sampler_border_color : uint8_t;
enum class primitive_topology : uint8_t;
enum class depth_function : uint8_t;
enum class cull_mode : uint8_t;
enum class rt_clear_type : uint8_t;
enum class blend_logic_op : uint8_t;
enum class blend_op : uint8_t;
enum class blend_factor : uint8_t;
enum class query_type : uint8_t;
enum class accel_struct_build_flags : uint8_t;

// helpers
struct command_stream_parser;
struct command_stream_writer;
}

namespace phi::arg
{
struct framebuffer_config;
struct vertex_format;

struct shader_arg_shape;
struct shader_binary;
struct graphics_shader;

using shader_arg_shapes = cc::span<shader_arg_shape const>;
using graphics_shaders = cc::span<graphics_shader const>;

struct blas_element;
struct raytracing_shader_library;
struct raytracing_argument_association;
struct raytracing_hit_group;
struct shader_table_record;

using raytracing_shader_libraries = cc::span<raytracing_shader_library const>;
using raytracing_argument_associations = cc::span<raytracing_argument_association const>;
using raytracing_hit_groups = cc::span<raytracing_hit_group const>;
using shader_table_records = cc::span<shader_table_record const>;

struct create_render_target_info;
struct create_texture_info;
struct create_buffer_info;
struct create_resource_info;

}
