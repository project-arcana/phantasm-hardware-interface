#pragma once

#include <clean-core/capped_vector.hh>
#include <clean-core/span.hh>

#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/common/native_enum.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/loader/volk.hh>
#include <phantasm-hardware-interface/vulkan/shader.hh>

namespace phi::vk
{
struct state_change
{
    resource_state before;
    resource_state after;
    VkPipelineStageFlags stages_before;
    VkPipelineStageFlags stages_after;

    explicit state_change(resource_state before, resource_state after, VkPipelineStageFlags before_dep, VkPipelineStageFlags after_dep)
      : before(before), after(after), stages_before(before_dep), stages_after(after_dep)
    {
    }
};

struct stage_dependencies
{
    VkPipelineStageFlags stages_before = 0;
    VkPipelineStageFlags stages_after = 0;

    stage_dependencies() = default;
    stage_dependencies(state_change const& initial_change) { add_change(initial_change); }

    void add_change(state_change const& change)
    {
        stages_before |= util::to_pipeline_stage_dependency(change.before, change.stages_before);
        stages_after |= util::to_pipeline_stage_dependency(change.after, change.stages_after);
    }

    void add_change(resource_state state_before, resource_state state_after, VkPipelineStageFlags shader_dep_before, VkPipelineStageFlags shader_dep_after)
    {
        stages_before |= util::to_pipeline_stage_dependency(state_before, shader_dep_before);
        stages_after |= util::to_pipeline_stage_dependency(state_after, shader_dep_after);
    }

    void reset()
    {
        stages_before = 0;
        stages_after = 0;
    }
};

[[nodiscard]] VkImageMemoryBarrier get_image_memory_barrier(
    VkImage image, state_change const& state_change, VkImageAspectFlags aspect, unsigned mip_start, unsigned num_mips, unsigned array_start, unsigned num_layers);

[[nodiscard]] VkBufferMemoryBarrier get_buffer_memory_barrier(VkBuffer buffer, state_change const& state_change, uint64_t buffer_size);


void submit_barriers(VkCommandBuffer cmd_buf,
                     stage_dependencies const& stage_deps,
                     cc::span<VkImageMemoryBarrier const> image_barriers,
                     cc::span<VkBufferMemoryBarrier const> buffer_barriers = {},
                     cc::span<VkMemoryBarrier const> barriers = {});

inline void submit_barriers(VkCommandBuffer cmd_buf,
                            state_change const& state_change,
                            cc::span<VkImageMemoryBarrier const> image_barriers,
                            cc::span<VkBufferMemoryBarrier const> buffer_barriers = {},
                            cc::span<VkMemoryBarrier const> barriers = {})
{
    stage_dependencies deps;
    deps.add_change(state_change);
    return submit_barriers(cmd_buf, deps, image_barriers, buffer_barriers, barriers);
}

template <size_t Nimg, size_t Nbuf = 0, size_t Nmem = 0>
struct barrier_bundle
{
    stage_dependencies dependencies;
    cc::capped_vector<VkImageMemoryBarrier, Nimg> barriers_img;
    cc::capped_vector<VkBufferMemoryBarrier, Nbuf> barriers_buf;
    cc::capped_vector<VkMemoryBarrier, Nmem> barriers_mem;

    // entire subresource barrier
    void add_image_barrier(VkImage image, state_change const& state_change, VkImageAspectFlags aspect)
    {
        dependencies.add_change(state_change);
        barriers_img.push_back(get_image_memory_barrier(image, state_change, aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS));
    }

    // specific level/slice barrier
    void add_image_barrier(VkImage image, state_change const& state_change, VkImageAspectFlags aspect, unsigned mip_slice, unsigned array_slice = 0)
    {
        dependencies.add_change(state_change);
        barriers_img.push_back(get_image_memory_barrier(image, state_change, aspect, mip_slice, 1, array_slice, 1));
    }

    void add_buffer_barrier(VkBuffer buffer, state_change const& state_change, uint64_t buffer_size)
    {
        dependencies.add_change(state_change);
        barriers_buf.push_back(get_buffer_memory_barrier(buffer, state_change, buffer_size));
    }

    [[nodiscard]] bool empty() const { return barriers_img.empty() && barriers_buf.empty() && barriers_mem.empty(); }

    /// Record contained barriers to the given cmd buffer
    void record(VkCommandBuffer cmd_buf)
    {
        if (!empty())
            submit_barriers(cmd_buf, dependencies, barriers_img, barriers_buf, barriers_mem);
    }

    void reset()
    {
        dependencies.reset();
        barriers_img.clear();
        barriers_buf.clear();
        barriers_mem.clear();
    }

    /// Record contained barriers to the given cmd buffer, close it, and submit it on the given queue
    void submit(VkCommandBuffer cmd_buf, VkQueue queue)
    {
        record(cmd_buf);
        vkEndCommandBuffer(cmd_buf);

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.pWaitDstStageMask = &dependencies.stages_before;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buf;

        PHI_VK_VERIFY_SUCCESS(vkQueueSubmit(queue, 1, &submit_info, nullptr));
    }
};


}
