#pragma once

#include <clean-core/assert.hh>

#include <phantasm-hardware-interface/common/format_size.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk::util
{
[[nodiscard]] constexpr VkAccessFlags to_access_flags(resource_state state)
{
    using rs = resource_state;
    switch (state)
    {
    case rs::undefined:
        return 0;
    case rs::vertex_buffer:
        return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    case rs::index_buffer:
        return VK_ACCESS_INDEX_READ_BIT;

    case rs::constant_buffer:
        return VK_ACCESS_UNIFORM_READ_BIT;
    case rs::shader_resource:
    case rs::shader_resource_nonpixel:
        return VK_ACCESS_SHADER_READ_BIT;
    case rs::unordered_access:
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    case rs::render_target:
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case rs::depth_read:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    case rs::depth_write:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    case rs::indirect_argument:
        return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    case rs::copy_src:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case rs::copy_dest:
        return VK_ACCESS_TRANSFER_WRITE_BIT;

    case rs::resolve_src:
        return VK_ACCESS_MEMORY_READ_BIT;
    case rs::resolve_dest:
        return VK_ACCESS_MEMORY_WRITE_BIT;

    case rs::present:
        return VK_ACCESS_MEMORY_READ_BIT;

    case rs::raytrace_accel_struct:
        return VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;

    case rs::unknown:
        CC_ASSERT(false && "unknown state access masks queried");
        return {};
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(state);
}

[[nodiscard]] constexpr VkImageLayout to_image_layout(resource_state state)
{
    using rs = resource_state;
    switch (state)
    {
    case rs::undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;

    case rs::shader_resource:
    case rs::shader_resource_nonpixel:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case rs::unordered_access:
        return VK_IMAGE_LAYOUT_GENERAL;

    case rs::render_target:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case rs::depth_read:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case rs::depth_write:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    case rs::copy_src:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case rs::copy_dest:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    case rs::resolve_src:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case rs::resolve_dest:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    case rs::present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // These do not apply to image layouts
    case rs::unknown:
    case rs::vertex_buffer:
    case rs::index_buffer:
    case rs::constant_buffer:
    case rs::indirect_argument:
    case rs::raytrace_accel_struct:
        CC_ASSERT(false && "invalid image layout queried");
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(state);
}

[[nodiscard]] constexpr VkPipelineStageFlags to_pipeline_stage_flags(phi::shader_stage stage)
{
    switch (stage)
    {
    case phi::shader_stage::pixel:
        return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case phi::shader_stage::vertex:
        return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    case phi::shader_stage::hull:
        return VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
    case phi::shader_stage::domain:
        return VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    case phi::shader_stage::geometry:
        return VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;

    case phi::shader_stage::compute:
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    case phi::shader_stage::ray_gen:
    case phi::shader_stage::ray_miss:
    case phi::shader_stage::ray_closest_hit:
    case phi::shader_stage::ray_intersect:
    case phi::shader_stage::ray_any_hit:
    case phi::shader_stage::ray_callable:
        return VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;

    case phi::shader_stage::none:
    case phi::shader_stage::MAX_SHADER_STAGE_RANGE:
        CC_ASSERT(false && "invalid shader stage given");
        return 0;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(stage);
}

[[nodiscard]] constexpr VkPipelineStageFlags to_pipeline_stage_flags_bitwise(phi::shader_stage_flags_t stage_flags)
{
    VkPipelineStageFlags res = 0;

    if (stage_flags & shader_stage::vertex)
        res |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;

    if (stage_flags & shader_stage::hull)
        res |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;

    if (stage_flags & shader_stage::domain)
        res |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;

    if (stage_flags & shader_stage::geometry)
        res |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;

    if (stage_flags & shader_stage::pixel)
        res |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;


    if (stage_flags & shader_stage::compute)
        res |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;


    if (stage_flags.has_any_of(shader_stage_mask_all_ray))
        res |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;

    return res;
}

[[nodiscard]] constexpr VkPipelineStageFlags to_pipeline_stage_dependency(resource_state state, VkPipelineStageFlags shader_flags)
{
    using rs = resource_state;
    switch (state)
    {
    case rs::undefined:
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    case rs::vertex_buffer:
    case rs::index_buffer:
        return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

    case rs::constant_buffer:
    case rs::shader_resource:
    case rs::shader_resource_nonpixel:
    case rs::unordered_access:
        return shader_flags;

    case rs::render_target:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    case rs::depth_read:
    case rs::depth_write:
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    case rs::indirect_argument:
        return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

    case rs::copy_src:
    case rs::copy_dest:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;

    case rs::resolve_src:
    case rs::resolve_dest:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;

    case rs::present:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // TODO: Not entirely sure about this, possible BOTTOM_OF_PIPELINE instead

    case rs::raytrace_accel_struct:
        return VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV;

    case rs::unknown:
        CC_ASSERT(false && "unknown state queried");
        return VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(state);
}

[[nodiscard]] constexpr VkPipelineStageFlags to_pipeline_stage_dependency(resource_state state, shader_stage stage = shader_stage::pixel)
{
    return to_pipeline_stage_dependency(state, to_pipeline_stage_flags(stage));
}

[[nodiscard]] constexpr VkPrimitiveTopology to_native(phi::primitive_topology topology)
{
    switch (topology)
    {
    case phi::primitive_topology::triangles:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case phi::primitive_topology::lines:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case phi::primitive_topology::points:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case phi::primitive_topology::patches:
        return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(topology);
}

[[nodiscard]] constexpr VkCompareOp to_native(phi::depth_function depth_func)
{
    switch (depth_func)
    {
    case phi::depth_function::none:
        return VK_COMPARE_OP_LESS; // sane defaults
    case phi::depth_function::less:
        return VK_COMPARE_OP_LESS;
    case phi::depth_function::less_equal:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case phi::depth_function::greater:
        return VK_COMPARE_OP_GREATER;
    case phi::depth_function::greater_equal:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case phi::depth_function::equal:
        return VK_COMPARE_OP_EQUAL;
    case phi::depth_function::not_equal:
        return VK_COMPARE_OP_NOT_EQUAL;
    case phi::depth_function::always:
        return VK_COMPARE_OP_ALWAYS;
    case phi::depth_function::never:
        return VK_COMPARE_OP_NEVER;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(depth_func);
}

[[nodiscard]] constexpr VkCullModeFlags to_native(phi::cull_mode cull_mode)
{
    switch (cull_mode)
    {
    case phi::cull_mode::none:
        return VK_CULL_MODE_NONE;
    case phi::cull_mode::back:
        return VK_CULL_MODE_BACK_BIT;
    case phi::cull_mode::front:
        return VK_CULL_MODE_FRONT_BIT;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(cull_mode);
}


[[nodiscard]] constexpr VkShaderStageFlagBits to_shader_stage_flags(phi::shader_stage stage)
{
    switch (stage)
    {
    case phi::shader_stage::pixel:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case phi::shader_stage::vertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case phi::shader_stage::domain:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case phi::shader_stage::hull:
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case phi::shader_stage::geometry:
        return VK_SHADER_STAGE_GEOMETRY_BIT;

    case phi::shader_stage::compute:
        return VK_SHADER_STAGE_COMPUTE_BIT;

    case phi::shader_stage::ray_gen:
        return VK_SHADER_STAGE_RAYGEN_BIT_NV;
    case phi::shader_stage::ray_miss:
        return VK_SHADER_STAGE_MISS_BIT_NV;
    case phi::shader_stage::ray_closest_hit:
        return VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
    case phi::shader_stage::ray_intersect:
        return VK_SHADER_STAGE_INTERSECTION_BIT_NV;
    case phi::shader_stage::ray_any_hit:
        return VK_SHADER_STAGE_ANY_HIT_BIT_NV;
    case phi::shader_stage::ray_callable:
        return VK_SHADER_STAGE_CALLABLE_BIT_NV;

    case phi::shader_stage::none:
    case phi::shader_stage::MAX_SHADER_STAGE_RANGE:
        CC_ASSERT(false && "invalid shader stage");
        return VK_SHADER_STAGE_ALL;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(stage);
}


[[nodiscard]] constexpr VkDescriptorType to_native_srv_desc_type(resource_view_dimension sv_dim)
{
    switch (sv_dim)
    {
    case phi::resource_view_dimension::buffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case phi::resource_view_dimension::raytracing_accel_struct:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
    case phi::resource_view_dimension::texture1d:
    case phi::resource_view_dimension::texture1d_array:
    case phi::resource_view_dimension::texture2d:
    case phi::resource_view_dimension::texture2d_ms:
    case phi::resource_view_dimension::texture2d_array:
    case phi::resource_view_dimension::texture2d_ms_array:
    case phi::resource_view_dimension::texture3d:
    case phi::resource_view_dimension::texturecube:
    case phi::resource_view_dimension::texturecube_array:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

    case phi::resource_view_dimension::none:
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(sv_dim);
}
[[nodiscard]] constexpr VkDescriptorType to_native_uav_desc_type(resource_view_dimension sv_dim)
{
    switch (sv_dim)
    {
    case phi::resource_view_dimension::buffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case phi::resource_view_dimension::texture1d:
    case phi::resource_view_dimension::texture1d_array:
    case phi::resource_view_dimension::texture2d:
    case phi::resource_view_dimension::texture2d_ms:
    case phi::resource_view_dimension::texture2d_array:
    case phi::resource_view_dimension::texture2d_ms_array:
    case phi::resource_view_dimension::texture3d:
    case phi::resource_view_dimension::texturecube:
    case phi::resource_view_dimension::texturecube_array:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    case phi::resource_view_dimension::raytracing_accel_struct:
    case phi::resource_view_dimension::none:
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(sv_dim);
}

[[nodiscard]] constexpr bool is_valid_as_uav_desc_type(resource_view_dimension sv_dim)
{
    return to_native_uav_desc_type(sv_dim) != VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

[[nodiscard]] constexpr VkImageViewType to_native_image_view_type(resource_view_dimension sv_dim)
{
    switch (sv_dim)
    {
    case phi::resource_view_dimension::texture1d:
        return VK_IMAGE_VIEW_TYPE_1D;
    case phi::resource_view_dimension::texture1d_array:
        return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case phi::resource_view_dimension::texture2d:
    case phi::resource_view_dimension::texture2d_ms:
        return VK_IMAGE_VIEW_TYPE_2D;
    case phi::resource_view_dimension::texture2d_array:
    case phi::resource_view_dimension::texture2d_ms_array:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case phi::resource_view_dimension::texture3d:
        return VK_IMAGE_VIEW_TYPE_3D;
    case phi::resource_view_dimension::texturecube:
        return VK_IMAGE_VIEW_TYPE_CUBE;
    case phi::resource_view_dimension::texturecube_array:
        return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;

    case phi::resource_view_dimension::buffer:
    case phi::resource_view_dimension::raytracing_accel_struct:
        CC_ASSERT(false && "requested image view for buffer or raytracing structure");
    case phi::resource_view_dimension::none:
        return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(sv_dim);
}

[[nodiscard]] constexpr VkImageAspectFlags to_native_image_aspect(format fmt)
{
    if (phi::util::is_view_format(fmt))
    {
        if (fmt == format::r24un_g8t)
        {
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else
        {
            CC_ASSERT(fmt == format::r24t_g8u && "unhandled view-type format");
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    else if (phi::util::is_depth_stencil_format(fmt))
    {
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    else if (phi::util::is_depth_format(fmt))
    {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    else
    {
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}


[[nodiscard]] constexpr VkFilter to_min_filter(sampler_filter filter)
{
    switch (filter)
    {
    case sampler_filter::min_mag_mip_point:
    case sampler_filter::min_point_mag_linear_mip_point:
    case sampler_filter::min_point_mag_mip_linear:
    case sampler_filter::min_mag_point_mip_linear:
        return VK_FILTER_NEAREST;
    case sampler_filter::min_linear_mag_mip_point:
    case sampler_filter::min_mag_linear_mip_point:
    case sampler_filter::min_linear_mag_point_mip_linear:
    case sampler_filter::min_mag_mip_linear:
    case sampler_filter::anisotropic:
        return VK_FILTER_LINEAR;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(filter);
}

[[nodiscard]] constexpr VkFilter to_mag_filter(sampler_filter filter)
{
    switch (filter)
    {
    case sampler_filter::min_mag_mip_point:
    case sampler_filter::min_linear_mag_mip_point:
    case sampler_filter::min_mag_point_mip_linear:
    case sampler_filter::min_linear_mag_point_mip_linear:
        return VK_FILTER_NEAREST;
    case sampler_filter::min_point_mag_linear_mip_point:
    case sampler_filter::min_point_mag_mip_linear:
    case sampler_filter::min_mag_linear_mip_point:
    case sampler_filter::min_mag_mip_linear:
    case sampler_filter::anisotropic:
        return VK_FILTER_LINEAR;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(filter);
}

[[nodiscard]] constexpr VkSamplerMipmapMode to_mipmap_filter(sampler_filter filter)
{
    switch (filter)
    {
    case sampler_filter::min_mag_mip_point:
    case sampler_filter::min_linear_mag_mip_point:
    case sampler_filter::min_mag_linear_mip_point:
    case sampler_filter::min_point_mag_linear_mip_point:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case sampler_filter::min_mag_point_mip_linear:
    case sampler_filter::min_linear_mag_point_mip_linear:
    case sampler_filter::min_point_mag_mip_linear:
    case sampler_filter::min_mag_mip_linear:
    case sampler_filter::anisotropic:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(filter);
}

[[nodiscard]] constexpr VkSamplerAddressMode to_native(sampler_address_mode mode)
{
    switch (mode)
    {
    case sampler_address_mode::wrap:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case sampler_address_mode::clamp:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case sampler_address_mode::clamp_border:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case sampler_address_mode::mirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(mode);
}

[[nodiscard]] constexpr VkCompareOp to_native(sampler_compare_func mode)
{
    switch (mode)
    {
    case sampler_compare_func::never:
    case sampler_compare_func::disabled:
        return VK_COMPARE_OP_NEVER;
    case sampler_compare_func::less:
        return VK_COMPARE_OP_LESS;
    case sampler_compare_func::equal:
        return VK_COMPARE_OP_EQUAL;
    case sampler_compare_func::less_equal:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case sampler_compare_func::greater:
        return VK_COMPARE_OP_GREATER;
    case sampler_compare_func::not_equal:
        return VK_COMPARE_OP_NOT_EQUAL;
    case sampler_compare_func::greater_equal:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case sampler_compare_func::always:
        return VK_COMPARE_OP_ALWAYS;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(mode);
}

[[nodiscard]] constexpr VkBorderColor to_native(sampler_border_color color)
{
    switch (color)
    {
    case sampler_border_color::black_transparent_float:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case sampler_border_color::black_transparent_int:
        return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    case sampler_border_color::black_float:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case sampler_border_color::black_int:
        return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case sampler_border_color::white_float:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    case sampler_border_color::white_int:
        return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(color);
}

[[nodiscard]] constexpr VkSampleCountFlagBits to_native_sample_flags(unsigned num_samples)
{
    switch (num_samples)
    {
    case 1:
        return VK_SAMPLE_COUNT_1_BIT;
    case 2:
        return VK_SAMPLE_COUNT_2_BIT;
    case 4:
        return VK_SAMPLE_COUNT_4_BIT;
    case 8:
        return VK_SAMPLE_COUNT_8_BIT;
    case 16:
        return VK_SAMPLE_COUNT_16_BIT;
    case 32:
        return VK_SAMPLE_COUNT_32_BIT;
    case 64:
        return VK_SAMPLE_COUNT_64_BIT;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(num_samples);
}

[[nodiscard]] constexpr VkAttachmentLoadOp to_native(rt_clear_type clear_type)
{
    switch (clear_type)
    {
    case rt_clear_type::load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case rt_clear_type::clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case rt_clear_type::dont_care:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(clear_type);
}

[[nodiscard]] constexpr VkImageType to_native(texture_dimension dim)
{
    switch (dim)
    {
    case texture_dimension::t1d:
        return VK_IMAGE_TYPE_1D;
    case texture_dimension::t2d:
        return VK_IMAGE_TYPE_2D;
    case texture_dimension::t3d:
        return VK_IMAGE_TYPE_3D;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(dim);
}

[[nodiscard]] constexpr VkLogicOp to_native(blend_logic_op op)
{
    switch (op)
    {
    case blend_logic_op::no_op:
        return VK_LOGIC_OP_NO_OP;
    case blend_logic_op::op_clear:
        return VK_LOGIC_OP_CLEAR;
    case blend_logic_op::op_set:
        return VK_LOGIC_OP_SET;
    case blend_logic_op::op_copy:
        return VK_LOGIC_OP_COPY;
    case blend_logic_op::op_copy_inverted:
        return VK_LOGIC_OP_COPY_INVERTED;
    case blend_logic_op::op_invert:
        return VK_LOGIC_OP_INVERT;
    case blend_logic_op::op_and:
        return VK_LOGIC_OP_AND;
    case blend_logic_op::op_nand:
        return VK_LOGIC_OP_NAND;
    case blend_logic_op::op_and_inverted:
        return VK_LOGIC_OP_AND_INVERTED;
    case blend_logic_op::op_and_reverse:
        return VK_LOGIC_OP_AND_REVERSE;
    case blend_logic_op::op_or:
        return VK_LOGIC_OP_OR;
    case blend_logic_op::op_nor:
        return VK_LOGIC_OP_NOR;
    case blend_logic_op::op_xor:
        return VK_LOGIC_OP_XOR;
    case blend_logic_op::op_or_reverse:
        return VK_LOGIC_OP_OR_REVERSE;
    case blend_logic_op::op_or_inverted:
        return VK_LOGIC_OP_OR_INVERTED;
    case blend_logic_op::op_equiv:
        return VK_LOGIC_OP_EQUIVALENT;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(op);
}

[[nodiscard]] constexpr VkBlendOp to_native(blend_op op)
{
    switch (op)
    {
    case blend_op::op_add:
        return VK_BLEND_OP_ADD;
    case blend_op::op_subtract:
        return VK_BLEND_OP_SUBTRACT;
    case blend_op::op_reverse_subtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case blend_op::op_min:
        return VK_BLEND_OP_MIN;
    case blend_op::op_max:
        return VK_BLEND_OP_MAX;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(op);
}

[[nodiscard]] constexpr VkBlendFactor to_native(blend_factor bf)
{
    switch (bf)
    {
    case blend_factor::zero:
        return VK_BLEND_FACTOR_ZERO;
    case blend_factor::one:
        return VK_BLEND_FACTOR_ONE;
    case blend_factor::src_color:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case blend_factor::inv_src_color:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case blend_factor::src_alpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case blend_factor::inv_src_alpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case blend_factor::dest_color:
        return VK_BLEND_FACTOR_DST_COLOR;
    case blend_factor::inv_dest_color:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case blend_factor::dest_alpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case blend_factor::inv_dest_alpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }

    CC_UNREACHABLE_SWITCH_WORKAROUND(bf);
}

[[nodiscard]] constexpr VkBuildAccelerationStructureFlagsNV to_native_accel_struct_build_flags(accel_struct_build_flags_t flags)
{
    VkBuildAccelerationStructureFlagsNV res = 0;

    if (flags & accel_struct_build_flags::allow_update)
        res |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
    if (flags & accel_struct_build_flags::allow_compaction)
        res |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_NV;
    if (flags & accel_struct_build_flags::prefer_fast_trace)
        res |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV;
    if (flags & accel_struct_build_flags::prefer_fast_build)
        res |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_NV;
    if (flags & accel_struct_build_flags::minimize_memory)
        res |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_NV;

    return res;
}
}
