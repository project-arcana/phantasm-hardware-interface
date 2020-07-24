#include "Backend.hh"

#include <phantasm-hardware-interface/arguments.hh>

phi::handle::resource phi::Backend::createBufferFromInfo(const phi::arg::create_buffer_info &info, const char *debug_name)
{
    return createBuffer(info.size_bytes, info.stride_bytes, info.heap, info.allow_uav, debug_name);
}

phi::handle::resource phi::Backend::createRenderTargetFromInfo(const phi::arg::create_render_target_info &info, const char *debug_name)
{
    return createRenderTarget(info.format, {info.width, info.height}, info.num_samples, info.array_size, &info.clear_value, debug_name);
}

phi::handle::resource phi::Backend::createTextureFromInfo(const phi::arg::create_texture_info &info, const char *debug_name)
{
    return createTexture(info.fmt, {info.width, info.height}, info.num_mips, info.dim, info.depth_or_array_size, info.allow_uav, debug_name);
}

phi::handle::resource phi::Backend::createResourceFromInfo(const phi::arg::create_resource_info &info, const char *debug_name)
{
    switch (info.type)
    {
    case arg::create_resource_info::e_resource_render_target:
        return createRenderTargetFromInfo(info.info_render_target, debug_name);
    case arg::create_resource_info::e_resource_texture:
        return createTextureFromInfo(info.info_texture, debug_name);
    case arg::create_resource_info::e_resource_buffer:
        return createBufferFromInfo(info.info_buffer, debug_name);
    default:
        CC_ASSERT(false && "invalid type");
        return handle::null_resource;
    }
    CC_UNREACHABLE("invalid type");
}
