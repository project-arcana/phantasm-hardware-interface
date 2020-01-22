#pragma once

#include <cstdint>

namespace phi
{
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

struct primitive_pipeline_config
{
    primitive_topology topology = primitive_topology::triangles;
    depth_function depth = depth_function::less;
    bool depth_readonly = false;
    cull_mode cull = cull_mode::back;
    int samples = 1;
};
}
