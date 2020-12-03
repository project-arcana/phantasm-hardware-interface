#pragma once

#include <mutex>

#include <clean-core/atomic_linked_pool.hh>
#include <clean-core/capped_vector.hh>
#include <clean-core/span.hh>
#include <clean-core/vector.hh>

#include <phantasm-hardware-interface/arguments.hh>
#include <phantasm-hardware-interface/limits.hh>

#include <phantasm-hardware-interface/vulkan/resources/descriptor_allocator.hh>

namespace phi::vk
{
class ResourcePool;
class AccelStructPool;

/// The high-level allocator for shader views
/// Synchronized
class ShaderViewPool
{
public:
    // frontend-facing API

    [[nodiscard]] handle::shader_view create(cc::span<resource_view const> srvs, cc::span<resource_view const> uavs, cc::span<sampler_config const> samplers, bool usage_compute);

    void free(handle::shader_view sv);
    void free(cc::span<handle::shader_view const> svs);

public:
    // internal API
    void initialize(VkDevice device, ResourcePool* res_pool, AccelStructPool* as_pool, unsigned num_cbvs, unsigned num_srvs, unsigned num_uavs, unsigned num_samplers, cc::allocator* static_alloc);
    void destroy();

    [[nodiscard]] VkDescriptorSet get(handle::shader_view sv) const { return mPool.get(sv._value).raw_desc_set; }

    [[nodiscard]] VkImageView makeImageView(resource_view const& sve, bool is_uav, bool restrict_usage_for_shader) const;


private:
    struct shader_view_node
    {
        VkDescriptorSet raw_desc_set;

        // the descriptor set layout used to create the descriptor set proper
        // This MUST stay alive, if it isn't alive, no warnings are emitted but
        // vkCmdBindDescriptorSets spuriously crashes the driver with compute binding points
        VkDescriptorSetLayout raw_desc_set_layout;

        // image views in use by this shader view
        cc::vector<VkImageView> image_views;
        // samplers in use by this shader view
        cc::vector<VkSampler> samplers;
    };

private:
    [[nodiscard]] VkSampler makeSampler(sampler_config const& config) const;

    void internalFree(shader_view_node& node) const;


private:
    // non-owning
    VkDevice mDevice = nullptr;
    ResourcePool* mResourcePool = nullptr;
    AccelStructPool* mAccelStructPool = nullptr;

    /// The main pool data
    cc::atomic_linked_pool<shader_view_node> mPool;

    /// "Backing" allocator
    DescriptorAllocator mAllocator;
    std::mutex mMutex;
};

}
