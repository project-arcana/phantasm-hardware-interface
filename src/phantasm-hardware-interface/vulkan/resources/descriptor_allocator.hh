#pragma once

#include <cstdint>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

#include <phantasm-hardware-interface/arguments.hh>

namespace pr::backend::vk
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
    [[nodiscard]] VkDescriptorSetLayout createSingleCBVLayout(bool usage_compute) const;

    [[nodiscard]] VkDescriptorSetLayout createLayoutFromShaderViewArgs(cc::span<shader_view_element const> srvs,
                                                                       cc::span<shader_view_element const> uavs,
                                                                       unsigned num_samplers,
                                                                       bool usage_compute) const;


    [[nodiscard]] VkDevice getDevice() const { return mDevice; }

private:
    VkDevice mDevice = nullptr;
    VkDescriptorPool mPool;
};

}
