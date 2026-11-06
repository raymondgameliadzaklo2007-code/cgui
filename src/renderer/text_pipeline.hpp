#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "font/font_atlas.hpp"

namespace cgui::renderer {

struct alignas(16) GlyphInstanceData {
    glm::vec4 rect;   // screen_x, screen_y, width, height (screen pixels)
    glm::vec4 uv;     // atlas u0, v0, u1, v1
    glm::vec4 color;  // r, g, b, a
    glm::vec4 params; // x = px_range, y = screen_width, z = screen_height, w = unused
};

class TextPipeline {
public:
    TextPipeline(VkDevice device, VkPhysicalDevice physical_device,
                 VkRenderPass render_pass, const font::FontAtlas& atlas);
    ~TextPipeline();

    TextPipeline(const TextPipeline&) = delete;
    TextPipeline& operator=(const TextPipeline&) = delete;

    [[nodiscard]] VkPipeline pipeline() const noexcept { return pipeline_; }
    [[nodiscard]] VkPipelineLayout layout() const noexcept { return layout_; }

    void bind(VkCommandBuffer cmd_buffer, VkExtent2D viewport_extent) const;
    void upload_instances(const std::vector<GlyphInstanceData>& instances);
    void draw(VkCommandBuffer cmd_buffer, uint32_t instance_count) const;

private:
    void create_atlas_texture(const font::FontAtlas& atlas);
    void create_descriptor_set();
    void create_unit_quad_buffer();
    void create_instance_buffer();
    void create_pipeline(VkRenderPass render_pass);

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                       VkBuffer& buffer, VkDeviceMemory& memory);

    VkDevice device_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};

    VkImage atlas_image_{VK_NULL_HANDLE};
    VkDeviceMemory atlas_memory_{VK_NULL_HANDLE};
    VkImageView atlas_view_{VK_NULL_HANDLE};
    VkSampler atlas_sampler_{VK_NULL_HANDLE};

    VkDescriptorSetLayout desc_layout_{VK_NULL_HANDLE};
    VkDescriptorPool desc_pool_{VK_NULL_HANDLE};
    VkDescriptorSet desc_set_{VK_NULL_HANDLE};

    VkPipelineLayout layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};

    VkBuffer unit_quad_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory unit_quad_memory_{VK_NULL_HANDLE};

    VkBuffer instance_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory instance_memory_{VK_NULL_HANDLE};
    void* mapped_instance_data_{nullptr};
    static constexpr size_t MAX_GLYPHS = 32768;
};

} // namespace cgui::renderer
