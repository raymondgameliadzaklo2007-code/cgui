/**
 * icon.c – Embedded SVG render + Vulkan textured quad
 */

#include "icon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <librsvg/rsvg.h>

typedef struct IconPush {
    float rect[4];
    float color[4];
    float screen[2];
    float _pad[2];
} IconPush;

static uint32_t find_mem_type(VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return 0;
}

static VkShaderModule make_module(VkDevice dev, const uint32_t *code, size_t words)
{
    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = words * sizeof(uint32_t),
        .pCode = code,
    };
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(dev, &ci, NULL, &m);
    return m;
}

bool icon_renderer_create(IconRenderer *r, VkContext *ctx,
                          const uint32_t *vert_spv, size_t vert_n,
                          const uint32_t *frag_spv, size_t frag_n)
{
    memset(r, 0, sizeof(*r));
    r->ctx = ctx;
    VkDevice dev = ctx->device;

    r->vert = make_module(dev, vert_spv, vert_n);
    r->frag = make_module(dev, frag_spv, frag_n);
    if (!r->vert || !r->frag) return false;

    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dsl = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    if (vkCreateDescriptorSetLayout(dev, &dsl, NULL, &r->set_layout) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize psz = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 64,
    };
    VkDescriptorPoolCreateInfo dpi = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 64,
        .poolSizeCount = 1,
        .pPoolSizes = &psz,
    };
    if (vkCreateDescriptorPool(dev, &dpi, NULL, &r->pool) != VK_SUCCESS)
        return false;

    VkPushConstantRange pcr = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(IconPush),
    };
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &r->set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr,
    };
    if (vkCreatePipelineLayout(dev, &plci, NULL, &r->layout) != VK_SUCCESS)
        return false;

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = r->vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = r->frag, .pName = "main" },
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState ba = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = 0xF,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &ba,
    };
    VkDynamicState dyns[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyns,
    };
    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vi, .pInputAssemblyState = &ia,
        .pViewportState = &vp, .pRasterizationState = &rs,
        .pMultisampleState = &ms, .pColorBlendState = &cb,
        .pDynamicState = &dyn, .layout = r->layout,
        .renderPass = ctx->render_pass, .subpass = 0,
    };
    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, NULL, &r->pipeline) != VK_SUCCESS)
        return false;

    r->ready = true;
    return true;
}

void icon_renderer_destroy(IconRenderer *r)
{
    if (!r || !r->ctx) return;
    VkDevice dev = r->ctx->device;
    vkDeviceWaitIdle(dev);
    if (r->pipeline) vkDestroyPipeline(dev, r->pipeline, NULL);
    if (r->layout) vkDestroyPipelineLayout(dev, r->layout, NULL);
    if (r->pool) vkDestroyDescriptorPool(dev, r->pool, NULL);
    if (r->set_layout) vkDestroyDescriptorSetLayout(dev, r->set_layout, NULL);
    if (r->vert) vkDestroyShaderModule(dev, r->vert, NULL);
    if (r->frag) vkDestroyShaderModule(dev, r->frag, NULL);
    memset(r, 0, sizeof(*r));
}

bool icon_load_svg_memory(Icon *out, VkContext *ctx,
                          const uint8_t *data, size_t data_size)
{
    memset(out, 0, sizeof(*out));
    if (!data || data_size == 0) {
        fprintf(stderr, "icon: invalid embedded SVG data\n");
        return false;
    }

    const int w = 24;
    const int h = 24;
    GError *error = NULL;
    RsvgHandle *svg = rsvg_handle_new_from_data(data, (gsize)data_size, &error);
    if (!svg) {
        fprintf(stderr, "icon: failed to parse embedded SVG: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "icon: failed to create SVG render surface\n");
        cairo_surface_destroy(surface);
        g_object_unref(svg);
        return false;
    }

    cairo_t *cr = cairo_create(surface);
    RsvgRectangle viewport = { .x = 0, .y = 0, .width = w, .height = h };
    gboolean rendered = rsvg_handle_render_document(svg, cr, &viewport, &error);
    cairo_destroy(cr);
    g_object_unref(svg);
    if (!rendered) {
        fprintf(stderr, "icon: failed to render embedded SVG: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        cairo_surface_destroy(surface);
        return false;
    }
    g_clear_error(&error);
    cairo_surface_flush(surface);

    size_t pixels_size = (size_t)w * h * 4;
    unsigned char *pixels = malloc(pixels_size);
    if (!pixels) {
        fprintf(stderr, "icon: failed to allocate SVG render buffer\n");
        cairo_surface_destroy(surface);
        return false;
    }
    const unsigned char *argb = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);

    /* Detect if the SVG is full-color (e.g. diagnostic error red, warning amber, info blue)
     * vs a monochrome icon mask (e.g. folder, chevron, file outlines) */
    bool has_color = false;
    for (int y = 0; y < h && !has_color; ++y) {
        const uint32_t *row = (const uint32_t *)(argb + (size_t)y * stride);
        for (int x = 0; x < w; ++x) {
            uint32_t pixel = row[x];
            uint8_t a = (uint8_t)(pixel >> 24);
            if (a > 20) {
                uint32_t pr = (pixel >> 16) & 0xFF;
                uint32_t pg = (pixel >> 8) & 0xFF;
                uint32_t pb = pixel & 0xFF;
                int ur = (int)((pr * 255) / a);
                int ug = (int)((pg * 255) / a);
                int ub = (int)((pb * 255) / a);
                if (abs(ur - ug) > 15 || abs(ug - ub) > 15 || abs(ur - ub) > 15) {
                    has_color = true;
                    break;
                }
            }
        }
    }

    for (int y = 0; y < h; ++y) {
        const uint32_t *row = (const uint32_t *)(argb + (size_t)y * stride);
        for (int x = 0; x < w; ++x) {
            uint32_t pixel = row[x];
            uint8_t a = (uint8_t)(pixel >> 24);
            size_t offset = ((size_t)y * w + x) * 4;
            if (has_color) {
                if (a == 0) {
                    pixels[offset + 0] = 0;
                    pixels[offset + 1] = 0;
                    pixels[offset + 2] = 0;
                    pixels[offset + 3] = 0;
                } else {
                    /* Un-premultiply Cairo ARGB32 to standard RGBA8 */
                    uint32_t pr = (pixel >> 16) & 0xFF;
                    uint32_t pg = (pixel >> 8) & 0xFF;
                    uint32_t pb = pixel & 0xFF;
                    pixels[offset + 0] = (uint8_t)((pr * 255) / a);
                    pixels[offset + 1] = (uint8_t)((pg * 255) / a);
                    pixels[offset + 2] = (uint8_t)((pb * 255) / a);
                    pixels[offset + 3] = a;
                }
            } else {
                /* Monochrome mask: RGB = 255, alpha from SVG, tinted at render time */
                pixels[offset + 0] = 255;
                pixels[offset + 1] = 255;
                pixels[offset + 2] = 255;
                pixels[offset + 3] = a;
            }
        }
    }
    cairo_surface_destroy(surface);
    out->w = w;
    out->h = h;

    VkDevice dev = ctx->device;
    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { (uint32_t)w, (uint32_t)h, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(dev, &ici, NULL, &out->image) != VK_SUCCESS) {
        free(pixels);
        return false;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, out->image, &req);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_mem_type(ctx->physical_device, req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    vkAllocateMemory(dev, &mai, NULL, &out->memory);
    vkBindImageMemory(dev, out->image, out->memory, 0);

    VkDeviceSize size = (VkDeviceSize)w * h * 4;
    VkBuffer staging;
    VkDeviceMemory staging_mem;
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    vkCreateBuffer(dev, &bci, NULL, &staging);
    VkMemoryRequirements breq;
    vkGetBufferMemoryRequirements(dev, staging, &breq);
    VkMemoryAllocateInfo bmai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = breq.size,
        .memoryTypeIndex = find_mem_type(ctx->physical_device, breq.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    vkAllocateMemory(dev, &bmai, NULL, &staging_mem);
    vkBindBufferMemory(dev, staging, staging_mem, 0);
    void *mapped = NULL;
    vkMapMemory(dev, staging_mem, 0, size, 0, &mapped);
    memcpy(mapped, pixels, size);
    vkUnmapMemory(dev, staging_mem);
    free(pixels);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(dev, &cai, &cmd);
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);
    VkImageMemoryBarrier to_dst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = out->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_dst);
    VkBufferImageCopy region = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { (uint32_t)w, (uint32_t)h, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging, out->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VkImageMemoryBarrier to_sample = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = out->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_sample);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vkQueueSubmit(ctx->graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->graphics_queue);
    vkFreeCommandBuffers(dev, ctx->command_pool, 1, &cmd);
    vkDestroyBuffer(dev, staging, NULL);
    vkFreeMemory(dev, staging_mem, NULL);

    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = out->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCreateImageView(dev, &vci, NULL, &out->view);
    VkSamplerCreateInfo sci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    vkCreateSampler(dev, &sci, NULL, &out->sampler);
    out->ready = true;
    printf("SVG icon loaded: %dx%d\n", w, h);
    return true;
}

void icon_destroy(Icon *icon, VkContext *ctx)
{
    if (!icon || !ctx) return;
    VkDevice dev = ctx->device;
    vkDeviceWaitIdle(dev);
    if (icon->sampler) vkDestroySampler(dev, icon->sampler, NULL);
    if (icon->view) vkDestroyImageView(dev, icon->view, NULL);
    if (icon->image) vkDestroyImage(dev, icon->image, NULL);
    if (icon->memory) vkFreeMemory(dev, icon->memory, NULL);
    memset(icon, 0, sizeof(*icon));
}

void icon_draw(IconRenderer *r, Icon *icon, VkContext *ctx,
               float x, float y, float w, float h, const uint8_t tint[4])
{
    if (!r || !r->ready || !icon || !icon->ready || !ctx || !ctx->frame_open) return;

    VkDevice dev = ctx->device;
    if (!icon->descriptor_set) {
        VkDescriptorSetAllocateInfo dsai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = r->pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &r->set_layout,
        };
        if (vkAllocateDescriptorSets(dev, &dsai, &icon->descriptor_set) != VK_SUCCESS)
            return;

        VkDescriptorImageInfo di = {
            .sampler = icon->sampler,
            .imageView = icon->view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = icon->descriptor_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &di,
        };
        vkUpdateDescriptorSets(dev, 1, &write, 0, NULL);
    }

    VkCommandBuffer cmd = ctx->active_cmd;
    VkViewport viewport = {
        .width = (float)ctx->swapchain_extent.width,
        .height = (float)ctx->swapchain_extent.height,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { .extent = ctx->swapchain_extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layout, 0, 1,
                            &icon->descriptor_set, 0, NULL);

    IconPush pc = {
        .rect = { x, y, w, h },
        .color = {
            tint ? tint[0] / 255.f : 1.f,
            tint ? tint[1] / 255.f : 1.f,
            tint ? tint[2] / 255.f : 1.f,
            tint ? tint[3] / 255.f : 1.f,
        },
        .screen = {
            (float)ctx->swapchain_extent.width,
            (float)ctx->swapchain_extent.height,
        },
    };
    vkCmdPushConstants(cmd, r->layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 6, 1, 0, 0);

}
