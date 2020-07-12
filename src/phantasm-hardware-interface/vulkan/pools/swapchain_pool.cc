#include "swapchain_pool.hh"

#include <clean-core/assert.hh>
#include <clean-core/utility.hh>

#include <phantasm-hardware-interface/detail/log.hh>

#include <phantasm-hardware-interface/vulkan/Device.hh>
#include <phantasm-hardware-interface/vulkan/common/verify.hh>
#include <phantasm-hardware-interface/vulkan/gpu_choice_util.hh>

namespace
{
constexpr VkFormat gc_assumed_backbuffer_format = VK_FORMAT_B8G8R8A8_UNORM;
}

phi::handle::swapchain phi::vk::SwapchainPool::createSwapchain(VkSurfaceKHR surface, int initial_w, int initial_h, unsigned num_backbuffers, phi::present_mode mode)
{
    handle::index_t res;
    {
        auto lg = std::lock_guard(mMutex);
        res = mPool.acquire();
    }

    swapchain& new_node = mPool.get(res);
    new_node.backbuf_width = -1;
    new_node.backbuf_height = -1;
    new_node.mode = mode;
    new_node.surface = surface;
    new_node.has_resized = true;
    new_node.active_fence_index = 0;
    new_node.active_image_index = 0;

    auto const surface_capabilities = get_surface_capabilities(mPhysicalDevice, surface);
    CC_RUNTIME_ASSERT(num_backbuffers >= surface_capabilities.minImageCount && "Not enough backbuffers specified");
    CC_RUNTIME_ASSERT(num_backbuffers <= 6 && "Too many backbuffers specified");
    CC_RUNTIME_ASSERT((surface_capabilities.maxImageCount == 0 || num_backbuffers <= surface_capabilities.maxImageCount) && "Too many backbuffers specified");

    auto const backbuffer_format_info = get_backbuffer_information(mPhysicalDevice, surface);
    new_node.backbuf_format = choose_backbuffer_format(backbuffer_format_info.backbuffer_formats);
    CC_RUNTIME_ASSERT(new_node.backbuf_format.format == gc_assumed_backbuffer_format && "Assumed backbuffer format wrong, please contact maintainers");

    cc::array<VkCommandBuffer> linear_cmd_buffers = cc::array<VkCommandBuffer>::uninitialized(num_backbuffers);

    // Create dummy command buffers in linear container
    {
        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = mDummyPresentCommandPool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = num_backbuffers;

        PHI_VK_VERIFY_SUCCESS(vkAllocateCommandBuffers(mDevice, &info, linear_cmd_buffers.data()));
    }

    // Create synchronization primitives and assign dummy command buffers
    new_node.backbuffers.emplace(num_backbuffers);
    for (auto i = 0u; i < new_node.backbuffers.size(); ++i)
    {
        auto& backbuffer = new_node.backbuffers[i];

        // assign and begin/end dummy command buffer
        {
            backbuffer.dummy_present_cmdbuf = linear_cmd_buffers[i];
            VkCommandBufferBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            PHI_VK_VERIFY_SUCCESS(vkBeginCommandBuffer(backbuffer.dummy_present_cmdbuf, &info));
            PHI_VK_VERIFY_SUCCESS(vkEndCommandBuffer(backbuffer.dummy_present_cmdbuf));
        }
        // create fence
        {
            VkFenceCreateInfo fence_info = {};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_info.pNext = nullptr;
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            PHI_VK_VERIFY_SUCCESS(vkCreateFence(mDevice, &fence_info, nullptr, &backbuffer.fence_command_buf_executed));
        }
        // create semaphores
        {
            VkSemaphoreCreateInfo sem_info = {};
            sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            sem_info.pNext = nullptr;
            sem_info.flags = 0;

            PHI_VK_VERIFY_SUCCESS(vkCreateSemaphore(mDevice, &sem_info, nullptr, &backbuffer.sem_image_available));
            PHI_VK_VERIFY_SUCCESS(vkCreateSemaphore(mDevice, &sem_info, nullptr, &backbuffer.sem_render_finished));
        }
    }


    // Create render pass
    {
        // color RT
        VkAttachmentDescription attachments[1];
        attachments[0].format = new_node.backbuf_format.format;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[0].flags = 0;

        VkAttachmentReference color_reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.flags = 0;
        subpass.inputAttachmentCount = 0;
        subpass.pInputAttachments = nullptr;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_reference;
        subpass.pResolveAttachments = nullptr;
        subpass.pDepthStencilAttachment = nullptr;
        subpass.preserveAttachmentCount = 0;
        subpass.pPreserveAttachments = nullptr;

        VkRenderPassCreateInfo rp_info = {};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.pNext = nullptr;
        rp_info.attachmentCount = 1;
        rp_info.pAttachments = attachments;
        rp_info.subpassCount = 1;
        rp_info.pSubpasses = &subpass;
        rp_info.dependencyCount = 0;
        rp_info.pDependencies = nullptr;

        PHI_VK_VERIFY_SUCCESS(vkCreateRenderPass(mDevice, &rp_info, nullptr, &mRenderPass));
    }

    auto res_handle = handle::swapchain{res};
    setupSwapchain(res_handle, initial_w, initial_h);
    return res_handle;
}

void phi::vk::SwapchainPool::free(phi::handle::swapchain handle)
{
    internalFree(mPool.get(handle._value));
    {
        auto lg = std::lock_guard(mMutex);
        mPool.release(handle._value);
    }
}

void phi::vk::SwapchainPool::onResize(phi::handle::swapchain handle, int w, int h)
{
    auto& node = mPool.get(handle._value);
    teardownSwapchain(node);
    setupSwapchain(handle, w, h);
}

bool phi::vk::SwapchainPool::present(phi::handle::swapchain handle)
{
    auto& node = mPool.get(handle._value);

    // perform present submit
    {
        auto& active_backbuffer = node.backbuffers[node.active_fence_index];

        vkResetFences(mDevice, 1, &active_backbuffer.fence_command_buf_executed);

        VkPipelineStageFlags submitWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.pWaitDstStageMask = &submitWaitStage;

        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &active_backbuffer.sem_image_available;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &active_backbuffer.sem_render_finished;

        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &active_backbuffer.dummy_present_cmdbuf;

        PHI_VK_VERIFY_SUCCESS(vkQueueSubmit(mPresentQueue, 1, &submit_info, active_backbuffer.fence_command_buf_executed));
    }

    // present proper
    {
        VkPresentInfoKHR present = {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.pNext = nullptr;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &node.backbuffers[node.active_fence_index].sem_render_finished;
        present.swapchainCount = 1;
        present.pSwapchains = &node.swapchain;
        present.pImageIndices = &node.active_image_index;
        present.pResults = nullptr;

        auto const present_res = vkQueuePresentKHR(mPresentQueue, &present);

        if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR)
        {
            onResize(handle, 0, 0);
            return false;
        }
        else
        {
            PHI_VK_ASSERT_SUCCESS(present_res);
        }

        node.active_fence_index = cc::wrapped_increment(node.active_fence_index, unsigned(node.backbuffers.size()));

        vkWaitForFences(mDevice, 1, &node.backbuffers[node.active_fence_index].fence_command_buf_executed, VK_TRUE, UINT64_MAX);
        return true;
    }
}

bool phi::vk::SwapchainPool::waitForBackbuffer(phi::handle::swapchain handle)
{
    auto& node = mPool.get(handle._value);
    auto const res = vkAcquireNextImageKHR(mDevice, node.swapchain, UINT64_MAX, node.backbuffers[node.active_fence_index].sem_image_available,
                                           nullptr, &node.active_image_index);

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        onResize(handle, 0, 0);
        return false;
    }
    else
    {
        PHI_VK_ASSERT_SUCCESS(res);
    }

    return true;
}

void phi::vk::SwapchainPool::initialize(const phi::vk::Device& device, const phi::backend_config& config)
{
    mDevice = device.getDevice();
    mPhysicalDevice = device.getPhysicalDevice();
    mPresentQueue = config.present_from_compute_queue ? device.getQueueCompute() : device.getQueueDirect();

    mPool.initialize(config.max_num_swapchains);

    // Create render pass
    {
        // color RT
        VkAttachmentDescription attachments[1];
        attachments[0].format = gc_assumed_backbuffer_format;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[0].flags = 0;

        VkAttachmentReference color_reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.flags = 0;
        subpass.inputAttachmentCount = 0;
        subpass.pInputAttachments = nullptr;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_reference;
        subpass.pResolveAttachments = nullptr;
        subpass.pDepthStencilAttachment = nullptr;
        subpass.preserveAttachmentCount = 0;
        subpass.pPreserveAttachments = nullptr;

        VkRenderPassCreateInfo rp_info = {};
        rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_info.pNext = nullptr;
        rp_info.attachmentCount = 1;
        rp_info.pAttachments = attachments;
        rp_info.subpassCount = 1;
        rp_info.pSubpasses = &subpass;
        rp_info.dependencyCount = 0;
        rp_info.pDependencies = nullptr;

        PHI_VK_VERIFY_SUCCESS(vkCreateRenderPass(mDevice, &rp_info, nullptr, &mRenderPass));
    }

    // Create dummy command pool
    {
        VkCommandPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.queueFamilyIndex = static_cast<unsigned>(device.getQueueFamilyDirect());
        PHI_VK_VERIFY_SUCCESS(vkCreateCommandPool(mDevice, &info, nullptr, &mDummyPresentCommandPool));
    }
}

void phi::vk::SwapchainPool::destroy()
{
    unsigned num_leaks = 0;
    mPool.iterate_allocated_nodes([&](swapchain& node) {
        internalFree(node);
        ++num_leaks;
    });

    if (num_leaks > 0)
    {
        PHI_LOG("leaked {} handle::swapchain object{}", num_leaks, (num_leaks == 1 ? "" : "s"));
    }

    vkDestroyRenderPass(mDevice, mRenderPass, nullptr);
    vkDestroyCommandPool(mDevice, mDummyPresentCommandPool, nullptr);
}

void phi::vk::SwapchainPool::setupSwapchain(phi::handle::swapchain handle, int width_hint, int height_hint)
{
    auto& node = mPool.get(handle._value);

    auto const surface_capabilities = get_surface_capabilities(mPhysicalDevice, node.surface);
    auto const present_format_info = get_backbuffer_information(mPhysicalDevice, node.surface);
    auto const new_extent = get_swap_extent(surface_capabilities, VkExtent2D{unsigned(width_hint), unsigned(height_hint)});

    node.backbuf_width = int(new_extent.width);
    node.backbuf_height = int(new_extent.height);
    node.has_resized = true;

    // Create swapchain
    {
        VkSwapchainCreateInfoKHR swapchain_info = {};
        swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_info.surface = node.surface;
        swapchain_info.imageFormat = node.backbuf_format.format;
        swapchain_info.imageColorSpace = node.backbuf_format.colorSpace;
        swapchain_info.minImageCount = unsigned(node.backbuffers.size());
        swapchain_info.imageExtent = new_extent;
        swapchain_info.imageArrayLayers = 1;
        swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // We require the graphics queue to be able to present
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_info.queueFamilyIndexCount = 0;
        swapchain_info.pQueueFamilyIndices = nullptr;

        swapchain_info.preTransform = choose_identity_transform(surface_capabilities);
        swapchain_info.compositeAlpha = choose_alpha_mode(surface_capabilities);
        swapchain_info.presentMode = choose_present_mode(present_format_info.present_modes, node.mode);

        swapchain_info.clipped = true;
        swapchain_info.oldSwapchain = nullptr;

        // NOTE: on some linux wms this causes false positive validation warnings, there is no workaround
        // see https://github.com/project-arcana/phantasm-hardware-interface/issues/26
        PHI_VK_VERIFY_SUCCESS(vkCreateSwapchainKHR(mDevice, &swapchain_info, nullptr, &node.swapchain));
    }

    // Query backbuffer VkImages
    cc::capped_array<VkImage, 6> backbuffer_images(node.backbuffers.size());
    {
        uint32_t num_backbuffers;
        // This is redundant, but the validation layer warns if we don't do this
        vkGetSwapchainImagesKHR(mDevice, node.swapchain, &num_backbuffers, nullptr);
        CC_ASSERT(num_backbuffers == node.backbuffers.size());
        vkGetSwapchainImagesKHR(mDevice, node.swapchain, &num_backbuffers, backbuffer_images.data());
    }

    // Set images, create RTVs and framebuffers
    for (auto i = 0u; i < node.backbuffers.size(); ++i)
    {
        auto& backbuffer = node.backbuffers[i];

        // Image
        backbuffer.image = backbuffer_images[i];

        backbuffer.state = resource_state::undefined;

        // RTV
        {
            VkImageViewCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = backbuffer.image;
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = node.backbuf_format.format;
            info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.baseMipLevel = 0;
            info.subresourceRange.levelCount = 1;
            info.subresourceRange.baseArrayLayer = 0;
            info.subresourceRange.layerCount = 1;
            PHI_VK_VERIFY_SUCCESS(vkCreateImageView(mDevice, &info, nullptr, &backbuffer.view));
        }

        // Framebuffer
        {
            VkImageView attachments[] = {backbuffer.view};


            VkFramebufferCreateInfo fb_info = {};
            fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb_info.pNext = nullptr;
            fb_info.renderPass = mRenderPass;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments = attachments;
            fb_info.width = new_extent.width;
            fb_info.height = new_extent.height;
            fb_info.layers = 1;

            PHI_VK_VERIFY_SUCCESS(vkCreateFramebuffer(mDevice, &fb_info, nullptr, &backbuffer.framebuffer));
        }
    }

    node.active_fence_index = 0;
    node.active_image_index = 0;
}

void phi::vk::SwapchainPool::teardownSwapchain(phi::vk::SwapchainPool::swapchain& node)
{
    vkDeviceWaitIdle(mDevice);
    for (auto& backbuffer : node.backbuffers)
    {
        vkDestroyFramebuffer(mDevice, backbuffer.framebuffer, nullptr);
        vkDestroyImageView(mDevice, backbuffer.view, nullptr);
    }
    vkDestroySwapchainKHR(mDevice, node.swapchain, nullptr);
}

void phi::vk::SwapchainPool::internalFree(phi::vk::SwapchainPool::swapchain& node)
{
    teardownSwapchain(node);

    for (auto const& backbuffer : node.backbuffers)
    {
        vkDestroyFence(mDevice, backbuffer.fence_command_buf_executed, nullptr);
        vkDestroySemaphore(mDevice, backbuffer.sem_image_available, nullptr);
        vkDestroySemaphore(mDevice, backbuffer.sem_render_finished, nullptr);
    }
}
