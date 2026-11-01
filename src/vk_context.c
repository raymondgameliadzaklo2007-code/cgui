/**
 * vk_context.c – Minimal but correct Vulkan 1.x context
 *
 * This is intentionally verbose and well-commented so the foundation is
 * easy to extend later (pipelines, descriptor sets, soft-shadow passes…).
 */

#include "vk_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void vk_check(VkResult res, const char *msg)
{
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Vulkan error (%d): %s\n", (int)res, msg);
    }
}

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ------------------------------------------------------------------ */
/* Instance                                                            */
/* ------------------------------------------------------------------ */

static bool create_instance(VkContext *ctx)
{
    /* SDL tells us which instance extensions are required for the surface */
    uint32_t ext_count = 0;
    const char *const *sdl_exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
    if (!sdl_exts) {
        fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions failed: %s\n", SDL_GetError());
        return false;
    }

    VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "cgui",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName        = "cgui",
        .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion         = VK_API_VERSION_1_1,
    };

    VkInstanceCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app_info,
        .enabledExtensionCount   = ext_count,
        .ppEnabledExtensionNames = sdl_exts,
        /* Validation layers can be added later when needed */
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = NULL,
    };

    VkResult res = vkCreateInstance(&ci, NULL, &ctx->instance);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed: %d\n", (int)res);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Surface                                                             */
/* ------------------------------------------------------------------ */

static bool create_surface(VkContext *ctx)
{
    if (!SDL_Vulkan_CreateSurface(ctx->window, ctx->instance, NULL, &ctx->surface)) {
        fprintf(stderr, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Physical device + queue families                                    */
/* ------------------------------------------------------------------ */

static bool pick_physical_device(VkContext *ctx)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &count, NULL);
    if (count == 0) {
        fprintf(stderr, "No Vulkan physical devices found\n");
        return false;
    }

    VkPhysicalDevice *devices = calloc(count, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(ctx->instance, &count, devices);

    /* Prefer a discrete GPU that can present to our surface */
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    uint32_t graphics_family = UINT32_MAX;
    uint32_t present_family  = UINT32_MAX;

    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDevice dev = devices[i];

        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, NULL);
        VkQueueFamilyProperties *qprops = calloc(qcount, sizeof(VkQueueFamilyProperties));
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops);

        uint32_t g = UINT32_MAX, p = UINT32_MAX;
        for (uint32_t q = 0; q < qcount; ++q) {
            if (qprops[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                g = q;
            }
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, q, ctx->surface, &present_support);
            if (present_support) {
                p = q;
            }
        }
        free(qprops);

        if (g != UINT32_MAX && p != UINT32_MAX) {
            chosen = dev;
            graphics_family = g;
            present_family  = p;
            /* Prefer discrete */
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                break;
            }
        }
    }
    free(devices);

    if (chosen == VK_NULL_HANDLE) {
        fprintf(stderr, "No suitable physical device\n");
        return false;
    }

    ctx->physical_device = chosen;
    ctx->graphics_family = graphics_family;
    ctx->present_family  = present_family;
    return true;
}

/* ------------------------------------------------------------------ */
/* Logical device                                                      */
/* ------------------------------------------------------------------ */

static bool create_device(VkContext *ctx)
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queues[2];
    uint32_t queue_count = 0;

    queues[queue_count++] = (VkDeviceQueueCreateInfo){
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->graphics_family,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };

    if (ctx->present_family != ctx->graphics_family) {
        queues[queue_count++] = (VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = ctx->present_family,
            .queueCount       = 1,
            .pQueuePriorities = &priority,
        };
    }

    const char *device_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = queue_count,
        .pQueueCreateInfos       = queues,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = device_exts,
        .pEnabledFeatures        = NULL,
    };

    if (vkCreateDevice(ctx->physical_device, &ci, NULL, &ctx->device) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDevice failed\n");
        return false;
    }

    vkGetDeviceQueue(ctx->device, ctx->graphics_family, 0, &ctx->graphics_queue);
    vkGetDeviceQueue(ctx->device, ctx->present_family,  0, &ctx->present_queue);
    return true;
}

/* ------------------------------------------------------------------ */
/* Swapchain                                                           */
/* ------------------------------------------------------------------ */

static bool create_swapchain(VkContext *ctx)
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device, ctx->surface, &caps);

    /* Format – prefer UNORM B8G8R8A8 */
    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &fmt_count, NULL);
    VkSurfaceFormatKHR *formats = calloc(fmt_count, sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &fmt_count, formats);

    VkSurfaceFormatKHR chosen_fmt = formats[0];
    for (uint32_t i = 0; i < fmt_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen_fmt = formats[i];
            break;
        }
    }
    free(formats);
    ctx->swapchain_format = chosen_fmt.format;

    /* Present mode – prefer MAILBOX, else FIFO (always available) */
    uint32_t pm_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &pm_count, NULL);
    VkPresentModeKHR *pmodes = calloc(pm_count, sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &pm_count, pmodes);

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < pm_count; ++i) {
        if (pmodes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }
    free(pmodes);

    /* Extent */
    if (caps.currentExtent.width != UINT32_MAX) {
        ctx->swapchain_extent = caps.currentExtent;
    } else {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(ctx->window, &w, &h);
        ctx->swapchain_extent.width  = clamp_u32((uint32_t)w, caps.minImageExtent.width,  caps.maxImageExtent.width);
        ctx->swapchain_extent.height = clamp_u32((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = ctx->surface,
        .minImageCount    = image_count,
        .imageFormat      = chosen_fmt.format,
        .imageColorSpace  = chosen_fmt.colorSpace,
        .imageExtent      = ctx->swapchain_extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = present_mode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE,
    };

    uint32_t qidx[2] = { ctx->graphics_family, ctx->present_family };
    if (ctx->graphics_family != ctx->present_family) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = qidx;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(ctx->device, &ci, NULL, &ctx->swapchain) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateSwapchainKHR failed\n");
        return false;
    }

    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->swapchain_image_count, NULL);
    ctx->swapchain_images = calloc(ctx->swapchain_image_count, sizeof(VkImage));
    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->swapchain_image_count, ctx->swapchain_images);

    /* Image views */
    ctx->swapchain_image_views = calloc(ctx->swapchain_image_count, sizeof(VkImageView));
    for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
        VkImageViewCreateInfo vci = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = ctx->swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = ctx->swapchain_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        if (vkCreateImageView(ctx->device, &vci, NULL, &ctx->swapchain_image_views[i]) != VK_SUCCESS) {
            fprintf(stderr, "vkCreateImageView failed\n");
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Render pass                                                         */
/* ------------------------------------------------------------------ */

static bool create_one_render_pass(VkContext *ctx, VkAttachmentLoadOp load_op,
                                    VkImageLayout initial, VkRenderPass *out)
{
    VkAttachmentDescription color = {
        .format         = ctx->swapchain_format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = load_op,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = initial,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_ref,
    };

    VkSubpassDependency dep = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &color,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dep,
    };

    if (vkCreateRenderPass(ctx->device, &ci, NULL, out) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateRenderPass failed\n");
        return false;
    }
    return true;
}

static bool create_render_pass(VkContext *ctx)
{
    if (!create_one_render_pass(ctx, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                VK_IMAGE_LAYOUT_UNDEFINED, &ctx->render_pass)) {
        return false;
    }
    /* Resume after offscreen work – keep existing pixels */
    if (!create_one_render_pass(ctx, VK_ATTACHMENT_LOAD_OP_LOAD,
                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &ctx->render_pass_load)) {
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Framebuffers                                                        */
/* ------------------------------------------------------------------ */

static bool create_framebuffers(VkContext *ctx)
{
    ctx->framebuffers = calloc(ctx->swapchain_image_count, sizeof(VkFramebuffer));
    for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
        VkImageView attachments[] = { ctx->swapchain_image_views[i] };
        VkFramebufferCreateInfo ci = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = ctx->render_pass,
            .attachmentCount = 1,
            .pAttachments    = attachments,
            .width           = ctx->swapchain_extent.width,
            .height          = ctx->swapchain_extent.height,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(ctx->device, &ci, NULL, &ctx->framebuffers[i]) != VK_SUCCESS) {
            fprintf(stderr, "vkCreateFramebuffer failed\n");
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Command pool + buffers + sync objects                               */
/* ------------------------------------------------------------------ */

static bool create_commands_and_sync(VkContext *ctx)
{
    VkCommandPoolCreateInfo pool_ci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphics_family,
    };
    if (vkCreateCommandPool(ctx->device, &pool_ci, NULL, &ctx->command_pool) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateCommandPool failed\n");
        return false;
    }

    VkCommandBufferAllocateInfo alloc = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx->command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = VK_MAX_FRAMES_IN_FLIGHT,
    };
    if (vkAllocateCommandBuffers(ctx->device, &alloc, ctx->command_buffers) != VK_SUCCESS) {
        fprintf(stderr, "vkAllocateCommandBuffers failed\n");
        return false;
    }

    VkSemaphoreCreateInfo sem_ci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_ci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(ctx->device, &sem_ci, NULL, &ctx->image_available[i]) != VK_SUCCESS ||
            vkCreateSemaphore(ctx->device, &sem_ci, NULL, &ctx->render_finished[i]) != VK_SUCCESS ||
            vkCreateFence(ctx->device, &fence_ci, NULL, &ctx->in_flight[i]) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create sync objects\n");
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool vk_context_create(VkContext *out, SDL_Window *window)
{
    memset(out, 0, sizeof(*out));
    out->window = window;
    out->clear_color[0] = 0.93f;   /* light macOS-ish grey */
    out->clear_color[1] = 0.93f;
    out->clear_color[2] = 0.94f;
    out->clear_color[3] = 1.0f;

    if (!create_instance(out))          return false;
    if (!create_surface(out))           return false;
    if (!pick_physical_device(out))     return false;
    if (!create_device(out))            return false;
    if (!create_swapchain(out))         return false;
    if (!create_render_pass(out))       return false;
    if (!create_framebuffers(out))      return false;
    if (!create_commands_and_sync(out)) return false;

    printf("Vulkan context ready – %u swapchain images, %ux%u\n",
           out->swapchain_image_count,
           out->swapchain_extent.width,
           out->swapchain_extent.height);
    return true;
}

void vk_context_destroy(VkContext *ctx)
{
    if (!ctx || !ctx->device) {
        return;
    }

    vkDeviceWaitIdle(ctx->device);

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ctx->image_available[i]) vkDestroySemaphore(ctx->device, ctx->image_available[i], NULL);
        if (ctx->render_finished[i]) vkDestroySemaphore(ctx->device, ctx->render_finished[i], NULL);
        if (ctx->in_flight[i])       vkDestroyFence(ctx->device, ctx->in_flight[i], NULL);
    }

    if (ctx->command_pool) vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);

    if (ctx->framebuffers) {
        for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
            vkDestroyFramebuffer(ctx->device, ctx->framebuffers[i], NULL);
        }
        free(ctx->framebuffers);
    }

    if (ctx->render_pass) vkDestroyRenderPass(ctx->device, ctx->render_pass, NULL);
    if (ctx->render_pass_load) vkDestroyRenderPass(ctx->device, ctx->render_pass_load, NULL);

    if (ctx->swapchain_image_views) {
        for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
            vkDestroyImageView(ctx->device, ctx->swapchain_image_views[i], NULL);
        }
        free(ctx->swapchain_image_views);
    }
    free(ctx->swapchain_images);

    if (ctx->swapchain) vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
    if (ctx->device)    vkDestroyDevice(ctx->device, NULL);
    if (ctx->surface)   vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
    if (ctx->instance)  vkDestroyInstance(ctx->instance, NULL);

    memset(ctx, 0, sizeof(*ctx));
}

bool vk_context_recreate_swapchain(VkContext *ctx)
{
    vkDeviceWaitIdle(ctx->device);

    /* Destroy old swapchain-dependent objects */
    for (uint32_t i = 0; i < ctx->swapchain_image_count; ++i) {
        vkDestroyFramebuffer(ctx->device, ctx->framebuffers[i], NULL);
        vkDestroyImageView(ctx->device, ctx->swapchain_image_views[i], NULL);
    }
    free(ctx->framebuffers);
    free(ctx->swapchain_image_views);
    free(ctx->swapchain_images);
    vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);

    ctx->framebuffers = NULL;
    ctx->swapchain_image_views = NULL;
    ctx->swapchain_images = NULL;
    ctx->swapchain = VK_NULL_HANDLE;

    if (!create_swapchain(ctx))    return false;
    if (!create_framebuffers(ctx)) return false;
    return true;
}

bool vk_context_begin_frame(VkContext *ctx)
{
    uint32_t frame = ctx->current_frame;

    vkWaitForFences(ctx->device, 1, &ctx->in_flight[frame], VK_TRUE, UINT64_MAX);

    uint32_t image_index = 0;
    VkResult res = vkAcquireNextImageKHR(ctx->device, ctx->swapchain, UINT64_MAX,
                                         ctx->image_available[frame], VK_NULL_HANDLE, &image_index);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "vkAcquireNextImageKHR failed: %d\n", (int)res);
        return false;
    }

    vkResetFences(ctx->device, 1, &ctx->in_flight[frame]);
    vkResetCommandBuffer(ctx->command_buffers[frame], 0);

    VkCommandBuffer cmd = ctx->command_buffers[frame];
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkBeginCommandBuffer(cmd, &begin);

    VkClearValue clear = {
        .color = {{ ctx->clear_color[0], ctx->clear_color[1],
                    ctx->clear_color[2], ctx->clear_color[3] }},
    };

    VkRenderPassBeginInfo rp = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = ctx->render_pass,
        .framebuffer     = ctx->framebuffers[image_index],
        .renderArea      = { .offset = {0, 0}, .extent = ctx->swapchain_extent },
        .clearValueCount = 1,
        .pClearValues    = &clear,
    };
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    ctx->active_cmd   = cmd;
    ctx->active_image = image_index;
    ctx->frame_open   = true;
    return true;
}

void vk_context_draw_rect(VkContext *ctx,
                          float x, float y, float w, float h,
                          const uint8_t rgba[4])
{
    if (!ctx || !ctx->frame_open || w <= 0.0f || h <= 0.0f) {
        return;
    }

    /* Clip to swapchain bounds */
    int32_t ix = (int32_t)x;
    int32_t iy = (int32_t)y;
    int32_t iw = (int32_t)w;
    int32_t ih = (int32_t)h;

    if (ix < 0) { iw += ix; ix = 0; }
    if (iy < 0) { ih += iy; iy = 0; }
    if (ix + iw > (int32_t)ctx->swapchain_extent.width) {
        iw = (int32_t)ctx->swapchain_extent.width - ix;
    }
    if (iy + ih > (int32_t)ctx->swapchain_extent.height) {
        ih = (int32_t)ctx->swapchain_extent.height - iy;
    }
    if (iw <= 0 || ih <= 0) {
        return;
    }

    VkClearAttachment ca = {
        .aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0,
        .clearValue = {
            .color = {{
                rgba[0] / 255.0f,
                rgba[1] / 255.0f,
                rgba[2] / 255.0f,
                rgba[3] / 255.0f,
            }},
        },
    };

    VkClearRect cr = {
        .rect = {
            .offset = { ix, iy },
            .extent = { (uint32_t)iw, (uint32_t)ih },
        },
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    vkCmdClearAttachments(ctx->active_cmd, 1, &ca, 1, &cr);
}

bool vk_context_end_frame(VkContext *ctx)
{
    if (!ctx->frame_open) {
        return true;
    }

    uint32_t frame = ctx->current_frame;
    VkCommandBuffer cmd = ctx->active_cmd;

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &ctx->image_available[frame],
        .pWaitDstStageMask    = &wait_stage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &ctx->render_finished[frame],
    };
    vkQueueSubmit(ctx->graphics_queue, 1, &submit, ctx->in_flight[frame]);

    VkPresentInfoKHR present = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &ctx->render_finished[frame],
        .swapchainCount     = 1,
        .pSwapchains        = &ctx->swapchain,
        .pImageIndices      = &ctx->active_image,
    };
    VkResult res = vkQueuePresentKHR(ctx->present_queue, &present);

    ctx->frame_open = false;
    ctx->active_cmd = VK_NULL_HANDLE;
    ctx->current_frame = (frame + 1) % VK_MAX_FRAMES_IN_FLIGHT;

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    return true;
}


void vk_context_suspend_pass(VkContext *ctx)
{
    if (!ctx || !ctx->frame_open) {
        return;
    }
    vkCmdEndRenderPass(ctx->active_cmd);
    ctx->frame_open = false;
}

void vk_context_resume_pass(VkContext *ctx)
{
    if (!ctx || ctx->frame_open) {
        return;
    }

    VkRenderPassBeginInfo rp = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = ctx->render_pass_load,
        .framebuffer     = ctx->framebuffers[ctx->active_image],
        .renderArea      = { .offset = {0, 0}, .extent = ctx->swapchain_extent },
        .clearValueCount = 0,
        .pClearValues    = NULL,
    };
    vkCmdBeginRenderPass(ctx->active_cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    ctx->frame_open = true;
}
