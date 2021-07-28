#pragma once

#include <clean-core/atomic_linked_pool.hh>
#include <clean-core/capped_vector.hh>

#include <phantasm-hardware-interface/fwd.hh>
#include <phantasm-hardware-interface/types.hh>

#include <phantasm-hardware-interface/vulkan/loader/volk.hh>

namespace phi::vk
{
class Device;
struct vulkan_gpu_info;

class SwapchainPool
{
public:
    struct backbuffer
    {
        // sync objects
        /// reset and signalled in ::performPresentSubmit, waited on (CPU) in ::present
        VkFence fence_command_buf_executed;
        /// signalled in ::acquireBackbuffer, waited on (GPU) in ::performPresentSubmit
        VkSemaphore sem_image_available;
        /// signalled in ::performPresentSubmit, waited on (GPU) in ::present
        VkSemaphore sem_render_finished;

        // dummy present command buffer
        VkCommandBuffer dummy_present_cmdbuf;

        // viewport-dependent resources
        VkImage image;
        VkImageView view;
        VkFramebuffer framebuffer;

        resource_state state;
    };

    struct swapchain
    {
        VkSwapchainKHR swapchain;
        VkSurfaceKHR surface;
        VkSurfaceFormatKHR backbuf_format;
        int backbuf_width;
        int backbuf_height;
        present_mode mode;
        bool has_resized;
        unsigned active_fence_index;
        unsigned active_image_index;
        cc::capped_vector<backbuffer, 6> backbuffers; ///< all backbuffers
    };

public:
    handle::swapchain createSwapchain(window_handle const& window_handle, int initial_w, int initial_h, unsigned num_backbuffers, present_mode mode, cc::allocator* scratch);

    void free(handle::swapchain handle);

    void onResize(handle::swapchain handle, int w, int h, cc::allocator* scratch);

    bool clearResizeFlag(handle::swapchain handle)
    {
        auto& node = mPool.get(handle._value);
        if (!node.has_resized)
            return false;

        node.has_resized = false;
        return true;
    }

    bool present(handle::swapchain handle, cc::allocator* scratch);

    bool acquireBackbuffer(handle::swapchain handle, cc::allocator* scratch);

    swapchain const& get(handle::swapchain handle) const { return mPool.get(handle._value); }

    unsigned getSwapchainIndex(handle::swapchain handle) const { return mPool.get_handle_index(handle._value); }

    void setBackbufferState(handle::swapchain handle, unsigned i, resource_state state) { mPool.get(handle._value).backbuffers[i].state = state; }

public:
    void initialize(VkInstance instance, Device const& device, const backend_config& config);
    void destroy();


private:
    void setupSwapchain(handle::swapchain handle, int width_hint, int height_hint, cc::allocator* scratch);

    void teardownSwapchain(swapchain& node);

    void internalFree(swapchain& node);

private:
    // nonowning
    VkInstance mInstance;
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice;
    VkQueue mPresentQueue;
    uint32_t mPresentQueueFamilyIndex;

    // owning
    cc::atomic_linked_pool<swapchain> mPool;

    VkRenderPass mRenderPass = nullptr;
    VkCommandPool mDummyPresentCommandPool;
};
}
