#pragma once

#include <mutex>

#include <clean-core/alloc_array.hh>
#include <clean-core/atomic_linked_pool.hh>
#include <clean-core/span.hh>

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

    [[nodiscard]] handle::shader_view create(cc::span<resource_view const> srvs,
                                             cc::span<resource_view const> uavs,
                                             cc::span<sampler_config const> samplers,
                                             bool usage_compute,
                                             cc::allocator* scratch);

    handle::shader_view createEmpty(arg::shader_view_description const& desc, bool usageCompute);

    void writeShaderViewSRVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> srvs, cc::allocator* scratch);

    void writeShaderViewUAVs(handle::shader_view sv, uint32_t offset, cc::span<resource_view const> uavs, cc::allocator* scratch);

    void writeShaderViewSamplers(handle::shader_view sv, uint32_t offset, cc::span<sampler_config const> samplers, cc::allocator* scratch);

    void copyShaderViewSRVs(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors);
    void copyShaderViewUAVs(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors);
    void copyShaderViewSamplers(handle::shader_view hDest, uint32_t offsetDest, handle::shader_view hSrc, uint32_t offsetSrc, uint32_t numDescriptors);

    void free(handle::shader_view sv);
    void free(cc::span<handle::shader_view const> svs);

public:
    // internal API
    void initialize(VkDevice device, ResourcePool* res_pool, AccelStructPool* as_pool, unsigned num_cbvs, unsigned num_srvs, unsigned num_uavs, unsigned num_samplers, cc::allocator* static_alloc);
    void destroy();

    [[nodiscard]] VkDescriptorSet get(handle::shader_view sv) const { return mPool.get(sv._value).descriptorSet; }

    [[nodiscard]] VkImageView makeImageView(resource_view const& sve, bool is_uav, bool restrict_usage_for_shader) const;


private:
    struct ShaderViewNode
    {
        VkDescriptorSet descriptorSet;

        // the descriptor set layout used to create the descriptor set proper
        // This MUST stay alive, if it isn't alive, no warnings are emitted but
        // vkCmdBindDescriptorSets spuriously crashes the driver with compute binding points
        VkDescriptorSetLayout descriptorSetLayout;

        uint32_t numSRVs = 0;

        // low memory: these are only accessed during shader_view updates, creation and destruction
        // we do not semantically require these, they just have to stay alive
        // image views in use by this shader view
        cc::alloc_array<VkImageView> imageViews;
        // samplers in use by this shader view
        cc::alloc_array<VkSampler> samplers;

        // optionally contains the descriptor entries for shader views that were created empty
        // this is required for a mapping from flat SRV/UAV descriptor indices to binding and array index
        cc::alloc_array<phi::arg::descriptor_entry> optionalDescriptorEntries;
        uint32_t numDescriptorEntriesSRV = 0;
    };

private:
    handle::shader_view createShaderViewFromLayout(VkDescriptorSetLayout layout,
                                                   uint32_t numSRVs,
                                                   uint32_t numUAVs,
                                                   uint32_t numSamplers,
                                                   cc::allocator* dynamicAlloc,
                                                   phi::arg::shader_view_description const* optDescription);

    [[nodiscard]] VkSampler makeSampler(sampler_config const& config) const;

    ShaderViewNode& internalGet(handle::shader_view res)
    {
        CC_ASSERT(res.is_valid() && "invalid shader_view handle");
        return mPool.get(res._value);
    }

    void internalFree(ShaderViewNode& node) const;

    // translates a flat index into a shader view's SRVs into the corresponding binding and array index
    // returns true on success
    bool flatSRVIndexToBindingAndArrayIndex(ShaderViewNode const& node, uint32_t flatIdx, uint32_t& outBinding, uint32_t& outArrayIndex) const;

    // translates a flat index into a shader view's UAVs into the corresponding binding and array index
    // returns true on success
    bool flatUAVIndexToBindingAndArrayIndex(ShaderViewNode const& node, uint32_t flatIdx, uint32_t& outBinding, uint32_t& outArrayIndex) const;

private:
    // non-owning
    VkDevice mDevice = nullptr;
    ResourcePool* mResourcePool = nullptr;
    AccelStructPool* mAccelStructPool = nullptr;

    /// The main pool data
    cc::atomic_linked_pool<ShaderViewNode> mPool;

    /// "Backing" allocator
    DescriptorAllocator mAllocator;
    std::mutex mMutex;
};

} // namespace phi::vk
