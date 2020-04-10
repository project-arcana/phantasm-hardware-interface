#include "BackendLog.hh"

#include <cstdio>
#include <cstdlib>

#include <thread>

#include <rich-log/log.hh>

#include <clean-core/macros.hh>
#include <clean-core/utility.hh>

#include "type_reflection.hh"

#define PHI_PRINT_DEBUG_FUNC() printf("\n[phi] " CC_PRETTY_FUNC "\n")

namespace
{
template <typename T>
void update_maximum(std::atomic<T>& maximum_value, T value) noexcept
{
    T prev_value = maximum_value;
    while (prev_value < value && !maximum_value.compare_exchange_weak(prev_value, value))
    {
    }
}
}

void phi::debug::BackendLog::initialize(const phi::backend_config& config, const phi::window_handle& window_handle)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

void phi::debug::BackendLog::destroy()
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

phi::debug::BackendLog::~BackendLog() { destroy(); }

void phi::debug::BackendLog::flushGPU()
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

phi::handle::resource phi::debug::BackendLog::acquireBackbuffer()
{
    PHI_PRINT_DEBUG_FUNC();
    return mDummyGuids.backbuffer;
}

void phi::debug::BackendLog::present()
{
    PHI_PRINT_DEBUG_FUNC();
    //
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(500ms);
}

void phi::debug::BackendLog::onResize(tg::isize2 size)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

phi::handle::resource phi::debug::BackendLog::createTexture(phi::format format, tg::isize2 size, unsigned mips, phi::texture_dimension dim, unsigned depth_or_array_size, bool allow_uav)
{
    PHI_PRINT_DEBUG_FUNC();
    //
    return {mDummyGuids.resource_guid.fetch_add(1)};
}

phi::handle::resource phi::debug::BackendLog::createRenderTarget(phi::format format, tg::isize2 size, unsigned samples, const rt_clear_value* opt_clear)
{
    PHI_PRINT_DEBUG_FUNC();
    //
    return {mDummyGuids.resource_guid.fetch_add(1)};
}

phi::handle::resource phi::debug::BackendLog::createBuffer(unsigned size_bytes, unsigned stride_bytes, bool allow_uav)
{
    PHI_PRINT_DEBUG_FUNC();
    //
    return {mDummyGuids.resource_guid.fetch_add(1)};
}

phi::handle::resource phi::debug::BackendLog::createMappedBuffer(unsigned size_bytes, unsigned stride_bytes)
{
    PHI_PRINT_DEBUG_FUNC();
    update_maximum(mMaxMappedSize, size_bytes);
    return {mDummyGuids.resource_guid.fetch_add(1)};
}

std::byte* phi::debug::BackendLog::getMappedMemory(phi::handle::resource res)
{
    PHI_PRINT_DEBUG_FUNC();
    //
    // we return an intenionally leaked allocation of maximum size so writes on the outside work as usual
    return static_cast<std::byte*>(std::malloc(mMaxMappedSize.load()));
}

void phi::debug::BackendLog::flushMappedMemory(phi::handle::resource)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

void phi::debug::BackendLog::free(phi::handle::resource res)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

void phi::debug::BackendLog::freeRange(cc::span<const phi::handle::resource> resources)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

phi::handle::shader_view phi::debug::BackendLog::createShaderView(cc::span<const phi::resource_view> srvs,
                                                                  cc::span<const phi::resource_view> uavs,
                                                                  cc::span<const phi::sampler_config> samplers,
                                                                  bool usage_compute)
{
    PHI_PRINT_DEBUG_FUNC();
    //
    return {mDummyGuids.sv_guid.fetch_add(1)};
}

void phi::debug::BackendLog::free(phi::handle::shader_view sv)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

void phi::debug::BackendLog::freeRange(cc::span<const phi::handle::shader_view> svs)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

phi::handle::pipeline_state phi::debug::BackendLog::createPipelineState(phi::arg::vertex_format vertex_format,
                                                                        const phi::arg::framebuffer_config& framebuffer_conf,
                                                                        phi::arg::shader_arg_shapes shader_arg_shapes,
                                                                        bool has_root_constants,
                                                                        phi::arg::graphics_shaders shaders,
                                                                        const phi::pipeline_config& primitive_config)
{
    PHI_PRINT_DEBUG_FUNC();
    //

    LOG(info) << vertex_format;
    LOG(info) << shader_arg_shapes;

    return {mDummyGuids.pso_guid.fetch_add(1)};
}

phi::handle::pipeline_state phi::debug::BackendLog::createComputePipelineState(phi::arg::shader_arg_shapes shader_arg_shapes, phi::arg::shader_binary shader, bool has_root_constants)
{
    PHI_PRINT_DEBUG_FUNC();
    //

    return {mDummyGuids.pso_guid.fetch_add(1)};
}

void phi::debug::BackendLog::free(phi::handle::pipeline_state ps)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

phi::handle::command_list phi::debug::BackendLog::recordCommandList(std::byte* buffer, size_t size, phi::handle::event event_to_set)
{
    PHI_PRINT_DEBUG_FUNC();
    //
    return {mDummyGuids.cmdlist_guid.fetch_add(1)};
}

void phi::debug::BackendLog::discard(cc::span<const phi::handle::command_list> cls)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

void phi::debug::BackendLog::submit(cc::span<const phi::handle::command_list> cls)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}

phi::handle::event phi::debug::BackendLog::createEvent()
{
    PHI_PRINT_DEBUG_FUNC();
    //
    return {mDummyGuids.event_guid.fetch_add(1)};
}

bool phi::debug::BackendLog::clearEvent(phi::handle::event event)
{
    PHI_PRINT_DEBUG_FUNC();
    //
    return true;
}

void phi::debug::BackendLog::free(cc::span<const phi::handle::event> events)
{
    PHI_PRINT_DEBUG_FUNC();
    //
}
