#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "vulkan_context.hpp"
#include "../ui/element.hpp"

namespace cgui::renderer {

struct QuadVertex {
    glm::vec2 pos;
    glm::vec2 uv;
};

struct QuadInstance {
    glm::vec4 rect;         // x, y, width, height (screen px)
    glm::vec4 color;        // r, g, b, a
    glm::vec4 border_color; // r, g, b, a
    glm::vec4 params;       // border_width, corner_radius, screen_width, screen_height
};

class Vulkan2DRenderer {
public:
    explicit Vulkan2DRenderer(VulkanContext& context);
    ~Vulkan2DRenderer();

    Vulkan2DRenderer(const Vulkan2DRenderer&) = delete;
    Vulkan2DRenderer& operator=(const Vulkan2DRenderer&) = delete;

    void begin_frame();
    void render_element_tree(const std::shared_ptr<ui::Element>& root);
    void end_frame();

private:
    void create_render_pass();
    void create_framebuffers();
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();
    void create_pipeline();

    void record_element_draws(const std::shared_ptr<ui::Element>& el, float parent_x, float parent_y);

    VulkanContext& context_;
    VkRenderPass render_pass_{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool command_pool_{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> command_buffers_;

    VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
    VkPipeline graphics_pipeline_{VK_NULL_HANDLE};

    std::vector<VkSemaphore> image_available_semaphores_;
    std::vector<VkSemaphore> render_finished_semaphores_;
    std::vector<VkFence> in_flight_fences_;

    size_t current_frame_{0};
    uint32_t current_image_index_{0};
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;
};

} // namespace cgui::renderer
