/**
 * icon.h – Render embedded SVG icons and draw as textured quads (Vulkan)
 *
 * Recommended sources (permissive):
 *   - Lucide (ISC) – same family Zed uses for many UI icons
 *   - Codicons (MIT)
 * Avoid copying Apple SF Symbols.
 */

#ifndef ICON_H
#define ICON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vk_context.h"

typedef struct Icon {
    int w, h;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    VkDescriptorSet descriptor_set;
    bool ready;
} Icon;

typedef struct IconRenderer {
    VkContext *ctx;
    VkShaderModule vert, frag;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    bool ready;
} IconRenderer;

bool icon_renderer_create(IconRenderer *r, VkContext *ctx,
                          const uint32_t *vert_spv, size_t vert_n,
                          const uint32_t *frag_spv, size_t frag_n);
void icon_renderer_destroy(IconRenderer *r);

bool icon_load_svg_memory(Icon *out, VkContext *ctx,
                          const uint8_t *data, size_t data_size);
void icon_destroy(Icon *icon, VkContext *ctx);

/** Draw icon in logical pixel rect (x,y,w,h). tint RGBA 0–255; NULL = white. */
void icon_draw(IconRenderer *r, Icon *icon, VkContext *ctx,
               float x, float y, float w, float h, const uint8_t tint[4]);

#endif
