#pragma once

#include <cstdint>

namespace pr
{
// see https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkPrimitiveTopology.html
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

// see https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkGraphicsPipelineCreateInfo.html
struct primitive_pipeline_config
{
    primitive_topology topology = primitive_topology::triangles;

    depth_function depth = depth_function::less;
    bool depth_readonly = false;

    cull_mode cull = cull_mode::back;

    int samples = 1;

    // TODO: detailed multisampling configuration
    // TODO: blend state
};
}
