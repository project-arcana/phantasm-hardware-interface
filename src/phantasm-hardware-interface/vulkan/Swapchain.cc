#include "Swapchain.hh"

#include "Device.hh"
#include "common/verify.hh"
#include "gpu_choice_util.hh"

void pr::backend::vk::Swapchain::initialize(const pr::backend::vk::Device& device, VkSurfaceKHR surface, unsigned num_backbuffers, int w, int h, present_mode sync)
{
    mSurface = surface;
    mDevice = device.getDevice();
    mPhysicalDevice = device.getPhysicalDevice();
    mPresentQueue = device.getQueueDirect();
    mBackbufferSize = tg::isize2(-1, -1);
    mSyncMode = sync;

    auto const surface_capabilities = get_surface_capabilities(mPhysicalDevice, mSurface);

    CC_RUNTIME_ASSERT(num_backbuffers >= surface_capabilities.minImageCount && "Not enough backbuffers specified");
    CC_RUNTIME_ASSERT(num_backbuffers < max_num_backbuffers && "Too many backbuffers specified");
    CC_RUNTIME_ASSERT(surface_capabilities.maxImageCount == 0 || num_backbuffers <= surface_capabilities.maxImageCount && "Too many backbuffers specified");

    auto const backbuffer_format_info = get_backbuffer_information(mPhysicalDevice, mSurface);
    mBackbufferFormat = choose_backbuffer_format(backbuffer_format_info.backbuffer_formats);

    // Create dummy command pool
    {
        VkCommandPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.queueFamilyIndex = static_cast<unsigned>(device.getQueueFamilyDirect());
        PR_VK_VERIFY_SUCCESS(vkCreateCommandPool(mDevice, &info, nullptr, &mDummyPresentCommandPool));
    }

    cc::array<VkCommandBuffer> linear_cmd_buffers = cc::array<VkCommandBuffer>::uninitialized(num_backbuffers);

    // Create dummy command buffers in linear container
    {
        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = mDummyPresentCommandPool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = num_backbuffers;

        PR_VK_VERIFY_SUCCESS(vkAllocateCommandBuffers(mDevice, &info, linear_cmd_buffers.data()));
    }

    // Create synchronization primitives and assign dummy command buffers
    mBackbuffers.emplace(num_backbuffers);
    for (auto i = 0u; i < mBackbuffers.size(); ++i)
    {
        auto& backbuffer = mBackbuffers[i];

        // assign and begin/end dummy command buffer
        {
            backbuffer.dummy_present_cmdbuf = linear_cmd_buffers[i];
            VkCommandBufferBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            PR_VK_VERIFY_SUCCESS(vkBeginCommandBuffer(backbuffer.dummy_present_cmdbuf, &info));
            PR_VK_VERIFY_SUCCESS(vkEndCommandBuffer(backbuffer.dummy_present_cmdbuf));
        }
        // create fence
        {
            VkFenceCreateInfo fence_info = {};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_info.pNext = nullptr;
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            PR_VK_VERIFY_SUCCESS(vkCreateFence(mDevice, &fence_info, nullptr, &backbuffer.fence_command_buf_executed));
        }
        // create semaphores
        {
            VkSemaphoreCreateInfo sem_info = {};
            sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            sem_info.pNext = nullptr;
            sem_info.flags = 0;

            PR_VK_VERIFY_SUCCESS(vkCreateSemaphore(mDevice, &sem_info, nullptr, &backbuffer.sem_image_available));
            PR_VK_VERIFY_SUCCESS(vkCreateSemaphore(mDevice, &sem_info, nullptr, &backbuffer.sem_render_finished));
        }
    }


    // Create render pass
    {
        // color RT
        VkAttachmentDescription attachments[1];
        attachments[0].format = mBackbufferFormat.format;
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

        PR_VK_VERIFY_SUCCESS(vkCreateRenderPass(mDevice, &rp_info, nullptr, &mRenderPass));
    }

    createSwapchain(w, h);
}

void pr::backend::vk::Swapchain::destroy()
{
    destroySwapchain();

    vkDestroyRenderPass(mDevice, mRenderPass, nullptr);

    vkDestroyCommandPool(mDevice, mDummyPresentCommandPool, nullptr);

    for (auto const& backbuffer : mBackbuffers)
    {
        vkDestroyFence(mDevice, backbuffer.fence_command_buf_executed, nullptr);
        vkDestroySemaphore(mDevice, backbuffer.sem_image_available, nullptr);
        vkDestroySemaphore(mDevice, backbuffer.sem_render_finished, nullptr);
    }
}

void pr::backend::vk::Swapchain::onResize(int width_hint, int height_hint)
{
    destroySwapchain();
    createSwapchain(width_hint, height_hint);
}

bool pr::backend::vk::Swapchain::present()
{
    VkPresentInfoKHR present = {};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = nullptr;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &mBackbuffers[mActiveFenceIndex].sem_render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &mSwapchain;
    present.pImageIndices = &mActiveImageIndex;
    present.pResults = nullptr;

    auto const present_res = vkQueuePresentKHR(mPresentQueue, &present);

    if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR)
    {
        onResize(0, 0);
        return false;
    }
    else
    {
        PR_VK_ASSERT_SUCCESS(present_res);
    }

    ++mActiveFenceIndex;
    if (mActiveFenceIndex >= mBackbuffers.size())
        mActiveFenceIndex -= static_cast<unsigned>(mBackbuffers.size());

    vkWaitForFences(mDevice, 1, &mBackbuffers[mActiveFenceIndex].fence_command_buf_executed, VK_TRUE, UINT64_MAX);
    return true;
}

bool pr::backend::vk::Swapchain::waitForBackbuffer()
{
    auto const res = vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, mBackbuffers[mActiveFenceIndex].sem_image_available, nullptr, &mActiveImageIndex);

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        onResize(0, 0);
        return false;
    }
    else
    {
        PR_VK_ASSERT_SUCCESS(res);
    }

    return true;
}

void pr::backend::vk::Swapchain::performPresentSubmit()
{
    auto& active_backbuffer = mBackbuffers[mActiveFenceIndex];

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

    PR_VK_VERIFY_SUCCESS(vkQueueSubmit(mPresentQueue, 1, &submit_info, active_backbuffer.fence_command_buf_executed));
}

void pr::backend::vk::Swapchain::createSwapchain(int width_hint, int height_hint)
{
    auto const surface_capabilities = get_surface_capabilities(mPhysicalDevice, mSurface);
    auto const present_format_info = get_backbuffer_information(mPhysicalDevice, mSurface);
    auto const new_extent = get_swap_extent(surface_capabilities, VkExtent2D{unsigned(width_hint), unsigned(height_hint)});
    mBackbufferSize = tg::isize2{int(new_extent.width), int(new_extent.height)};
    mBackbufferHasResized = true;

    // Create swapchain
    {
        VkSwapchainCreateInfoKHR swapchain_info = {};
        swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_info.surface = mSurface;
        swapchain_info.imageFormat = mBackbufferFormat.format;
        swapchain_info.imageColorSpace = mBackbufferFormat.colorSpace;
        swapchain_info.minImageCount = unsigned(mBackbuffers.size());
        swapchain_info.imageExtent = new_extent;
        swapchain_info.imageArrayLayers = 1;
        swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // We require the graphics queue to be able to present
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_info.queueFamilyIndexCount = 0;
        swapchain_info.pQueueFamilyIndices = nullptr;

        swapchain_info.preTransform = choose_identity_transform(surface_capabilities);
        swapchain_info.compositeAlpha = choose_alpha_mode(surface_capabilities);
        swapchain_info.presentMode = choose_present_mode(present_format_info.present_modes, mSyncMode);

        swapchain_info.clipped = true;
        swapchain_info.oldSwapchain = nullptr;

        PR_VK_VERIFY_SUCCESS(vkCreateSwapchainKHR(mDevice, &swapchain_info, nullptr, &mSwapchain));
    }

    // Query backbuffer VkImages
    cc::capped_array<VkImage, max_num_backbuffers> backbuffer_images(mBackbuffers.size());
    {
        uint32_t num_backbuffers;
        // This is redundant, but the validation layer warns if we don't do this
        vkGetSwapchainImagesKHR(mDevice, mSwapchain, &num_backbuffers, nullptr);
        CC_RUNTIME_ASSERT(num_backbuffers == mBackbuffers.size());
        vkGetSwapchainImagesKHR(mDevice, mSwapchain, &num_backbuffers, backbuffer_images.data());
    }

    // Set images, create RTVs and framebuffers
    for (auto i = 0u; i < mBackbuffers.size(); ++i)
    {
        auto& backbuffer = mBackbuffers[i];

        // Image
        backbuffer.image = backbuffer_images[i];

        backbuffer.state = resource_state::undefined;

        // RTV
        {
            VkImageViewCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = backbuffer.image;
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = mBackbufferFormat.format;
            info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.baseMipLevel = 0;
            info.subresourceRange.levelCount = 1;
            info.subresourceRange.baseArrayLayer = 0;
            info.subresourceRange.layerCount = 1;
            PR_VK_VERIFY_SUCCESS(vkCreateImageView(mDevice, &info, nullptr, &backbuffer.view));
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

            PR_VK_VERIFY_SUCCESS(vkCreateFramebuffer(mDevice, &fb_info, nullptr, &backbuffer.framebuffer));
        }
    }

    mActiveFenceIndex = 0;
    mActiveImageIndex = 0;
}

void pr::backend::vk::Swapchain::destroySwapchain()
{
    vkDeviceWaitIdle(mDevice);
    for (auto& backbuffer : mBackbuffers)
    {
        vkDestroyFramebuffer(mDevice, backbuffer.framebuffer, nullptr);
        vkDestroyImageView(mDevice, backbuffer.view, nullptr);
    }
    vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
}
