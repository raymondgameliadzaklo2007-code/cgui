#include "vulkan_2d.hpp"
#include <stdexcept>
#include <iostream>
#include <array>

namespace cgui::renderer {

Vulkan2DRenderer::Vulkan2DRenderer(VulkanContext& context) : context_(context) {
    create_render_pass();
    create_framebuffers();
    create_command_pool();
    create_command_buffers();
    create_sync_objects();

    quad_pipeline_ = std::make_unique<QuadPipeline>(
        context_.device(),
        context_.physical_device(),
        render_pass_
    );

    quad_instances_.reserve(2048);
}

Vulkan2DRenderer::~Vulkan2DRenderer() {
    VkDevice dev = context_.device();

    context_.wait_idle();

    quad_pipeline_.reset();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (render_finished_semaphores_[i]) vkDestroySemaphore(dev, render_finished_semaphores_[i], nullptr);
        if (image_available_semaphores_[i]) vkDestroySemaphore(dev, image_available_semaphores_[i], nullptr);
        if (in_flight_fences_[i]) vkDestroyFence(dev, in_flight_fences_[i], nullptr);
    }

    if (command_pool_) {
        vkDestroyCommandPool(dev, command_pool_, nullptr);
    }

    cleanup_framebuffers();

    if (render_pass_) {
        vkDestroyRenderPass(dev, render_pass_, nullptr);
    }
}

void Vulkan2DRenderer::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = context_.swapchain_image_format();
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    if (vkCreateRenderPass(context_.device(), &render_pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan render pass");
    }
}

void Vulkan2DRenderer::create_framebuffers() {
    const auto& image_views = context_.swapchain_image_views();
    framebuffers_.resize(image_views.size());

    VkExtent2D extent = context_.swapchain_extent();

    for (size_t i = 0; i < image_views.size(); ++i) {
        VkImageView attachments[] = {image_views[i]};

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass_;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = extent.width;
        framebuffer_info.height = extent.height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(context_.device(), &framebuffer_info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan framebuffer");
        }
    }
}

void Vulkan2DRenderer::cleanup_framebuffers() {
    VkDevice dev = context_.device();
    for (auto framebuffer : framebuffers_) {
        if (framebuffer) {
            vkDestroyFramebuffer(dev, framebuffer, nullptr);
        }
    }
    framebuffers_.clear();
}

void Vulkan2DRenderer::on_swapchain_resized() {
    cleanup_framebuffers();
    create_framebuffers();
}

void Vulkan2DRenderer::create_command_pool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = 0;

    if (vkCreateCommandPool(context_.device(), &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool");
    }
}

void Vulkan2DRenderer::create_command_buffers() {
    command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());

    if (vkAllocateCommandBuffers(context_.device(), &alloc_info, command_buffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan command buffers");
    }
}

void Vulkan2DRenderer::create_sync_objects() {
    image_available_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkDevice dev = context_.device();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(dev, &semaphore_info, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(dev, &semaphore_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(dev, &fence_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects for a frame");
        }
    }
}

void Vulkan2DRenderer::begin_frame() {
    VkDevice dev = context_.device();
    vkWaitForFences(dev, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(dev, context_.swapchain(), UINT64_MAX,
                                            image_available_semaphores_[current_frame_], VK_NULL_HANDLE,
                                            &current_image_index_);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        context_.recreate_swapchain();
        on_swapchain_resized();
        frame_started_ = false;
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swapchain image");
    }

    vkResetFences(dev, 1, &in_flight_fences_[current_frame_]);
    vkResetCommandBuffer(command_buffers_[current_frame_], 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(command_buffers_[current_frame_], &begin_info);

    quad_instances_.clear();
    frame_started_ = true;
}

void Vulkan2DRenderer::collect_instances(const std::shared_ptr<ui::Element>& el,
                                        float parent_x, float parent_y,
                                        float screen_w, float screen_h) {
    if (!el) return;

    auto bounds = el->computed_bounds();
    float abs_x = parent_x + bounds.x;
    float abs_y = parent_y + bounds.y;

    const auto& style = el->style();

    // If element has a visible background or border, add a quad instance
    if (style.background.has_value() || (style.border_color.has_value() && style.border_width > 0.0f)) {
        QuadInstanceData inst{};
        inst.rect = glm::vec4(abs_x, abs_y, bounds.width, bounds.height);

        if (el->is_hovered() && style.hover_background.has_value()) {
            const auto& hbg = style.hover_background.value();
            inst.color = glm::vec4(hbg.r, hbg.g, hbg.b, hbg.a);
        } else if (style.background.has_value()) {
            const auto& bg = style.background.value();
            inst.color = glm::vec4(bg.r, bg.g, bg.b, bg.a);
        } else {
            inst.color = glm::vec4(0.0f);
        }

        if (style.border_color.has_value()) {
            const auto& bc = style.border_color.value();
            inst.border_color = glm::vec4(bc.r, bc.g, bc.b, bc.a);
        } else {
            inst.border_color = glm::vec4(0.0f);
        }

        inst.params = glm::vec4(style.border_width, style.corner_radius, screen_w, screen_h);
        quad_instances_.push_back(inst);
    }

    // Traverse children in layout order
    for (const auto& child : el->children()) {
        collect_instances(child, abs_x, abs_y, screen_w, screen_h);
    }
}

void Vulkan2DRenderer::render_element_tree(const std::shared_ptr<ui::Element>& root) {
    if (!frame_started_ || !root) return;

    auto extent = context_.swapchain_extent();
    float screen_w = static_cast<float>(extent.width);
    float screen_h = static_cast<float>(extent.height);

    // 1. Calculate Yoga Flexbox Layout for the entire tree
    root->calculate_layout(screen_w, screen_h);

    // 2. Traverse and collect all styled quads
    collect_instances(root, 0.0f, 0.0f, screen_w, screen_h);

    // 3. Upload instances to dynamic ring buffer
    quad_pipeline_->upload_instances(quad_instances_);

    // 4. Record render pass and instanced draw command
    VkCommandBuffer cmd = command_buffers_[current_frame_];

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = render_pass_;
    render_pass_info.framebuffer = framebuffers_[current_image_index_];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;

    VkClearValue clear_color = {{{0.09f, 0.10f, 0.12f, 1.0f}}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    quad_pipeline_->bind(cmd, extent);
    quad_pipeline_->draw(cmd, static_cast<uint32_t>(quad_instances_.size()));

    vkCmdEndRenderPass(cmd);
}

void Vulkan2DRenderer::end_frame() {
    if (!frame_started_) return;

    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = {image_available_semaphores_[current_frame_]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    VkSemaphore signal_semaphores[] = {render_finished_semaphores_[current_frame_]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(context_.graphics_queue(), 1, &submit_info, in_flight_fences_[current_frame_]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    VkSwapchainKHR swapchains[] = {context_.swapchain()};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &current_image_index_;

    VkResult result = vkQueuePresentKHR(context_.present_queue(), &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        context_.recreate_swapchain();
        on_swapchain_resized();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image");
    }

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    frame_started_ = false;
}

} // namespace cgui::renderer
