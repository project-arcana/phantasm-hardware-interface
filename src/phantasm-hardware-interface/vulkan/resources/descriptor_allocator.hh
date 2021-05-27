#pragma once

#include <cstdint>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

#include <phantasm-hardware-interface/arguments.hh>

namespace phi::vk
{
/// Unsynchronized
class DescriptorAllocator
{
public:
    void initialize(VkDevice device, uint32_t num_cbvs, uint32_t num_srvs, uint32_t num_uavs, uint32_t num_samplers);
    void destroy();

    // requires sync
    [[nodiscard]] VkDescriptorSet allocDescriptor(VkDescriptorSetLayout descriptorLayout);

    // requires sync
    void free(VkDescriptorSet descriptor_set);

    // free-threaded
    VkDescriptorSetLayout createSingleCBVLayout(bool usage_compute) const;

    VkDescriptorSetLayout createLayoutFromShaderViewArgs(cc::span<resource_view const> srvs, cc::span<resource_view const> uavs, unsigned num_samplers, bool usage_compute) const;

    VkDescriptorSetLayout createLayoutFromDescription(arg::shader_view_description const& desc, bool usageCompute);


    VkDevice getDevice() const { return mDevice; }

private:
    VkDevice mDevice = nullptr;
    VkDescriptorPool mPool;
};

} // namespace phi::vk
