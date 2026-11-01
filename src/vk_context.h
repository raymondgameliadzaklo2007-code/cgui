/**
 * vk_context.h – Minimal Vulkan context for a single window
 *
 * Owns: instance, surface, device, swapchain, render pass, sync.
 * Exposes a simple immediate-mode style API for solid rectangles
 * (implemented with vkCmdClearAttachments – no shaders required yet).
 */

#ifndef VK_CONTEXT_H
#define VK_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#define VK_MAX_FRAMES_IN_FLIGHT 2

typedef struct VkContext {
    SDL_Window *window;
    VkInstance  instance;
    VkSurfaceKHR surface;

    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          graphics_queue;
    VkQueue          present_queue;
    uint32_t         graphics_family;
    uint32_t         present_family;

    VkSwapchainKHR   swapchain;
    VkFormat         swapchain_format;
    VkExtent2D       swapchain_extent;
    VkImage         *swapchain_images;
    VkImageView     *swapchain_image_views;
    uint32_t         swapchain_image_count;

    VkRenderPass     render_pass;       /* loadOp = CLEAR */
    VkRenderPass     render_pass_load;  /* loadOp = LOAD  (resume after offscreen) */
    VkFramebuffer   *framebuffers;

    VkCommandPool    command_pool;
    VkCommandBuffer  command_buffers[VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore      image_available[VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore      render_finished[VK_MAX_FRAMES_IN_FLIGHT];
    VkFence          in_flight[VK_MAX_FRAMES_IN_FLIGHT];
    uint32_t         current_frame;

    /* Per-frame state (valid between begin_frame and end_frame) */
    VkCommandBuffer  active_cmd;
    uint32_t         active_image;
    bool             frame_open;

    float clear_color[4];
} VkContext;

bool vk_context_create(VkContext *out, SDL_Window *window);
void vk_context_destroy(VkContext *ctx);
bool vk_context_recreate_swapchain(VkContext *ctx);

/**
 * Begin a frame: acquire image, start command buffer + render pass.
 * Returns false if the swapchain needs recreation.
 */
bool vk_context_begin_frame(VkContext *ctx);

/**
 * Fill an axis-aligned rectangle with a solid colour (RGBA 0–255).
 * Must be called between begin_frame and end_frame.
 * Coordinates are in pixels, origin top-left (same as SDL).
 */
void vk_context_draw_rect(VkContext *ctx,
                          float x, float y, float w, float h,
                          const uint8_t rgba[4]);

/**
 * End the render pass, submit, and present.
 * Returns false if the swapchain needs recreation.
 */
bool vk_context_end_frame(VkContext *ctx);

/** Temporarily end the main render pass (for offscreen work). */
void vk_context_suspend_pass(VkContext *ctx);

/** Resume main pass with LOAD so previous content is kept. */
void vk_context_resume_pass(VkContext *ctx);

#endif /* VK_CONTEXT_H */
