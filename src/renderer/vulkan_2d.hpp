#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "vulkan_context.hpp"
#include "quad_pipeline.hpp"
#include "../ui/element.hpp"

namespace cgui::renderer {

class Vulkan2DRenderer {
public:
    explicit Vulkan2DRenderer(VulkanContext& context);
    ~Vulkan2DRenderer();

    Vulkan2DRenderer(const Vulkan2DRenderer&) = delete;
    Vulkan2DRenderer& operator=(const Vulkan2DRenderer&) = delete;

    void on_swapchain_resized();

    void begin_frame();
    void render_element_tree(const std::shared_ptr<ui::Element>& root);
    void end_frame();

private:
    void create_render_pass();
    void create_framebuffers();
    void cleanup_framebuffers();
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();

    void collect_instances(const std::shared_ptr<ui::Element>& el, float parent_x, float parent_y,
                           float screen_w, float screen_h);

    VulkanContext& context_;
    VkRenderPass render_pass_{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool command_pool_{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> command_buffers_;

    std::unique_ptr<QuadPipeline> quad_pipeline_;

    std::vector<VkSemaphore> image_available_semaphores_;
    std::vector<VkSemaphore> render_finished_semaphores_;
    std::vector<VkFence> in_flight_fences_;

    std::vector<QuadInstanceData> quad_instances_;

    size_t current_frame_{0};
    uint32_t current_image_index_{0};
    bool frame_started_{false};
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;
};

} // namespace cgui::renderer
