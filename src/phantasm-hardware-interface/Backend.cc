#include "Backend.hh"

#include <phantasm-hardware-interface/common/byte_util.hh>
#include <phantasm-hardware-interface/common/format_size.hh>

#include <phantasm-hardware-interface/arguments.hh>

phi::init_status phi::Backend::initializeParallel(backend_config const& /*config*/, uint32_t /*idx*/)
{
    // do nothing by default
    return init_status::success;
}

phi::init_status phi::Backend::initializeQueues(backend_config const& /*config*/)
{
    // do nothing by default
    return init_status::success;
}

phi::handle::resource phi::Backend::createTexture(
    phi::format format, tg::isize2 size, uint32_t mips, texture_dimension dim, uint32_t depth_or_array_size, bool allow_uav, char const* debug_name)
{
    arg::texture_description desc = {};
    desc.fmt = format;
    desc.dim = dim;
    desc.usage = allow_uav ? resource_usage_flags::allow_uav : 0;
    desc.width = size.width;
    desc.height = size.height;
    desc.depth_or_array_size = depth_or_array_size;
    desc.num_mips = mips;
    desc.num_samples = 1;

    return createTexture(desc, debug_name);
}

phi::handle::resource phi::Backend::createRenderTarget(
    phi::format format, tg::isize2 size, uint32_t samples, uint32_t array_size, rt_clear_value const* optimized_clear_val, char const* debug_name)
{
    arg::texture_description desc = {};
    desc.fmt = format;
    desc.dim = texture_dimension::t2d;
    desc.usage = util::is_depth_format(format) ? resource_usage_flags::allow_depth_stencil : resource_usage_flags::allow_render_target;
    desc.width = size.width;
    desc.height = size.height;
    desc.depth_or_array_size = array_size;
    desc.num_mips = 1;
    desc.num_samples = samples;

    if (optimized_clear_val)
    {
        desc.usage |= resource_usage_flags::use_optimized_clear_value;
        desc.optimized_clear_value = util::pack_rgba8(optimized_clear_val->red_or_depth, optimized_clear_val->green_or_stencil,
                                                      optimized_clear_val->blue, optimized_clear_val->alpha);
    }

    return createTexture(desc, debug_name);
}

phi::handle::resource phi::Backend::createBuffer(uint32_t size_bytes, uint32_t stride_bytes, resource_heap heap, bool allow_uav, char const* debug_name)
{
    arg::buffer_description desc = {};
    desc.size_bytes = size_bytes;
    desc.stride_bytes = stride_bytes;
    desc.heap = heap;
    desc.allow_uav = allow_uav;

    return createBuffer(desc, debug_name);
}

phi::handle::resource phi::Backend::createUploadBuffer(uint32_t size_bytes, uint32_t stride_bytes, char const* debug_name)
{
    arg::buffer_description desc = {};
    desc.size_bytes = size_bytes;
    desc.stride_bytes = stride_bytes;
    desc.heap = resource_heap::upload;
    desc.allow_uav = false;

    return createBuffer(desc, debug_name);
}

phi::handle::resource phi::Backend::createResourceFromInfo(const phi::arg::resource_description& info, const char* debug_name)
{
    switch (info.type)
    {
    case arg::resource_description::e_resource_texture:
        return createTexture(info.info_texture, debug_name);
    case arg::resource_description::e_resource_buffer:
        return createBuffer(info.info_buffer, debug_name);
    default:
        CC_ASSERT(false && "invalid type");
        return handle::null_resource;
    }
    CC_UNREACHABLE("invalid type");
}

phi::handle::pipeline_state phi::Backend::createComputePipelineState(arg::shader_arg_shapes arg_shapes, arg::shader_binary shader, bool hasRootConsts)
{
    arg::compute_pipeline_state_description desc = {};
    for (auto const& arg : arg_shapes)
    {
        desc.root_signature.shader_arg_shapes.push_back(arg);
    }
    desc.root_signature.has_root_constants = false;
    desc.shader = shader;

    return createComputePipelineState(desc);
}
