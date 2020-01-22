#pragma once

namespace pr
{
struct primitive_pipeline_config;
}

namespace pr::backend
{
struct shader_argument;
struct backend_config;
struct vertex_attribute_info;
struct shader_view_element;
struct sampler_config;
struct render_target_config;
struct window_handle;

struct gpu_info;

struct command_stream_parser;
struct command_stream_writer;

class Backend;
}

namespace pr::backend::arg
{
struct framebuffer_config;
struct vertex_format;
struct shader_argument_shape;
struct shader_stage;
}
