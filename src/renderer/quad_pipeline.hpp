#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace cgui::renderer {

class VulkanContext;

struct UnitVertex {
    glm::vec2 pos;
};

struct alignas(16) QuadInstanceData {
    glm::vec4 rect;         // x, y, width, height (screen pixels)
    glm::vec4 color;        // r, g, b, a (fill)
    glm::vec4 border_color; // r, g, b, a (border)
    glm::vec4 params;       // border_width, corner_radius, screen_width, screen_height
};

class QuadPipeline {
public:
    QuadPipeline(VkDevice device, VkPhysicalDevice physical_device, VkRenderPass render_pass);
    ~QuadPipeline();

    QuadPipeline(const QuadPipeline&) = delete;
    QuadPipeline& operator=(const QuadPipeline&) = delete;

    [[nodiscard]] VkPipeline pipeline() const noexcept { return pipeline_; }
    [[nodiscard]] VkPipelineLayout layout() const noexcept { return layout_; }

    void bind(VkCommandBuffer cmd_buffer, VkExtent2D viewport_extent) const;
    void upload_instances(const std::vector<QuadInstanceData>& instances);
    void draw(VkCommandBuffer cmd_buffer, uint32_t instance_count) const;

private:
    void create_unit_quad_buffer();
    void create_instance_buffer();
    void create_pipeline(VkRenderPass render_pass);

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                       VkBuffer& buffer, VkDeviceMemory& memory);

    VkDevice device_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};

    VkPipelineLayout layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};

    // Static geometry: unit quad (2 triangles, 6 vertices)
    VkBuffer unit_quad_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory unit_quad_memory_{VK_NULL_HANDLE};

    // Dynamic instance ring buffer
    VkBuffer instance_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory instance_memory_{VK_NULL_HANDLE};
    void* mapped_instance_data_{nullptr};
    static constexpr size_t MAX_QUADS = 16384;
};

} // namespace cgui::renderer
