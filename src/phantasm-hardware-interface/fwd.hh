#pragma once

#include <cstdint>

namespace phi
{
struct shader_argument;
struct backend_config;
struct vertex_attribute_info;
struct resource_view;
struct sampler_config;
struct render_target_config;
struct window_handle;
struct pipeline_config;

struct gpu_info;

struct command_stream_parser;
struct command_stream_writer;

class Backend;

enum class format : uint8_t;
}

namespace phi::arg
{
struct framebuffer_config;
struct vertex_format;
struct shader_arg_shape;
struct graphics_shader;
}
