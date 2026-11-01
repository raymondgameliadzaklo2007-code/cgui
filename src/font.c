/**
 * font.c – FreeType light hinting + HarfBuzz shaping + pixel-exact Vulkan quads
 *
 * What makes small text crisp here:
 *   1. Glyphs are rasterized once, at integer physical pixel size, with FreeType
 *      light hinting (vertical snapping only) into an R8 atlas.
 *   2. Every glyph quad is placed on whole device pixels with a size equal to
 *      its bitmap, so one atlas texel maps to exactly one screen pixel.
 *   3. Fixed-width fonts use integer device-pixel advances, so columns stay on
 *      a perfectly even grid (no 8/8/9 jitter between glyphs).
 *   4. The fragment shader adjusts coverage by text luminance (see text.frag).
 *
 * Coordinates: font_draw()/font_measure() and all Font metrics are in logical
 * UI units; device pixels = logical * render_scale, and the swapchain extent is
 * assumed to be the physical framebuffer size.
 */

#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>

/* Ligatures / contextual alternates break per-column cursor math in an editor.
   Build with -DFONT_LIGATURES=1 to let HarfBuzz apply them. */
#ifndef FONT_LIGATURES
#define FONT_LIGATURES 0
#endif

/* FreeType stem darkening thickens small stems *before* rasterizing (CFF and
   the auto-hinter), closer to Core Text weight than a shader curve alone.
   On by default; build with -DFONT_STEM_DARKENING=0 to disable. If text now
   feels too heavy, lower the weight with font_set_tuning(). */
#ifndef FONT_STEM_DARKENING
#define FONT_STEM_DARKENING 1
#endif

#define ATLAS_SIZE          2048
#define ATLAS_PAD           2      /* gap between glyphs in the atlas */
#define MAX_EXTRA_GLYPHS    3000   /* non-ASCII glyphs baked at startup */
#define MIN_PIXEL_SIZE      4

#if FONT_LIGATURES
#define SHAPE_FEATURES   NULL
#define SHAPE_N_FEATURES 0
#else
static const hb_feature_t k_features[] = {
    { HB_TAG('l','i','g','a'), 0, 0, (unsigned int)-1 },
    { HB_TAG('c','l','i','g'), 0, 0, (unsigned int)-1 },
    { HB_TAG('c','a','l','t'), 0, 0, (unsigned int)-1 },
};
#define SHAPE_FEATURES   k_features
#define SHAPE_N_FEATURES 3
#endif

/* Per-draw constants (32 bytes). */
typedef struct TextPush {
    float color[4];
    float screen[2];
    float tune[2];      /* weight, contrast */
} TextPush;

/* Per-glyph instance data (32 bytes), read as two vec4 vertex attributes. */
typedef struct GlyphInstance {
    float rect[4];      /* x, y, w, h in device pixels */
    float uv[4];        /* u0, v0, u1, v1 */
} GlyphInstance;

/* The ring is written by the CPU while earlier frames may still be in flight,
   so it must hold several frames of glyphs. 8 MB = ~260k glyphs; an editor
   screen is ~10k, so this is far beyond any reasonable frames-in-flight. */
#define RING_BYTES  (8u * 1024u * 1024u)
#define BATCH_MAX   2048    /* glyphs reserved/flushed per sub-batch */

#define VK_TRY(expr)                                                     \
    do {                                                                 \
        if ((expr) != VK_SUCCESS) {                                      \
            fprintf(stderr, "font: %s failed\n", #expr);                 \
            goto done;                                                   \
        }                                                                \
    } while (0)

static char *dup_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* ------------------------------------------------------------------------ */
/* Vulkan helpers                                                            */
/* ------------------------------------------------------------------------ */

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
    if (vkCreateShaderModule(dev, &ci, NULL, &m) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return m;
}

/* ------------------------------------------------------------------------ */
/* Atlas baking                                                              */
/* ------------------------------------------------------------------------ */

typedef struct Packer { int x, y, row_h; } Packer;
enum { PACK_OK = 0, PACK_SKIP = 1, PACK_FULL = -1 };

/* Rasterize one glyph with light hinting and shelf-pack it into the atlas. */
static int pack_glyph(Font *f, FT_Face face, FT_UInt gindex, int shift26,
                      Packer *pk, FontGlyph *fg)
{
    if (FT_Load_Glyph(face, gindex, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP) != 0)
        return PACK_SKIP;
    FT_GlyphSlot g = face->glyph;
    /* Sub-pixel variant: shift the hinted outline horizontally before rasterizing. */
    if (shift26 && g->format == FT_GLYPH_FORMAT_OUTLINE)
        FT_Outline_Translate(&g->outline, shift26, 0);
    if (g->format != FT_GLYPH_FORMAT_BITMAP &&
        FT_Render_Glyph(g, FT_RENDER_MODE_NORMAL) != 0)
        return PACK_SKIP;

    const FT_Bitmap *bmp = &g->bitmap;
    int bw = (int)bmp->width;
    int bh = (int)bmp->rows;
    if (bw > 0 && bmp->pixel_mode != FT_PIXEL_MODE_GRAY)
        return PACK_SKIP;                       /* colour/mono bitmaps don't fit R8 */

    if (bw > 0 && bh > 0) {
        if (pk->x + bw + ATLAS_PAD > f->atlas_w) {
            pk->x = ATLAS_PAD;
            pk->y += pk->row_h + ATLAS_PAD;
            pk->row_h = 0;
        }
        if (pk->y + bh + ATLAS_PAD > f->atlas_h)
            return PACK_FULL;

        for (int row = 0; row < bh; ++row) {
            const unsigned char *src = bmp->buffer + (ptrdiff_t)row * bmp->pitch;
            unsigned char *dst = f->atlas_pixels
                               + (size_t)(pk->y + row) * (size_t)f->atlas_w + (size_t)pk->x;
            memcpy(dst, src, (size_t)bw);
        }
    } else {
        bw = bh = 0;
    }

    const float inv = 1.0f / f->render_scale;
    memset(fg, 0, sizeof(*fg));
    fg->present = true;
    fg->glyph_index = gindex;
    fg->advance = ((float)g->advance.x / 64.0f) * inv;
    fg->x0 = (float)g->bitmap_left * inv;
    fg->y0 = -(float)g->bitmap_top * inv;
    fg->x1 = fg->x0 + (float)bw * inv;
    fg->y1 = fg->y0 + (float)bh * inv;
    if (bw > 0) {
        fg->u0 = (float)pk->x / (float)f->atlas_w;
        fg->v0 = (float)pk->y / (float)f->atlas_h;
        fg->u1 = (float)(pk->x + bw) / (float)f->atlas_w;
        fg->v1 = (float)(pk->y + bh) / (float)f->atlas_h;
        pk->x += bw + ATLAS_PAD;
        if (bh > pk->row_h) pk->row_h = bh;
    }
    return PACK_OK;
}

static bool bake_atlas(Font *f, FT_Face face)
{
    f->atlas_w = f->atlas_h = ATLAS_SIZE;
    f->atlas_pixels = calloc((size_t)f->atlas_w * (size_t)f->atlas_h, 1);
    f->by_index_count = (unsigned int)face->num_glyphs;
    f->by_index = calloc(f->by_index_count ? f->by_index_count : 1, sizeof(FontGlyph));
    if (!f->atlas_pixels || !f->by_index)
        return false;

    Packer pk = { ATLAS_PAD, ATLAS_PAD, 0 };
    bool full = false;

    /* ASCII first so it is guaranteed to fit. */
    for (int c = 32; c < 128 && !full; ++c) {
        FT_UInt gi = FT_Get_Char_Index(face, (FT_ULong)c);
        if (gi >= f->by_index_count) continue;
        if (!f->by_index[gi].present) {
            int r = pack_glyph(f, face, gi, 0, &pk, &f->by_index[gi]);
            if (r == PACK_FULL) { full = true; break; }
        }
        if (f->by_index[gi].present)
            f->glyphs[c] = f->by_index[gi];
    }

    /* Fixed-width detection: the flag, or identical advances for 'i' and 'M'. */
    f->mono = FT_IS_FIXED_WIDTH(face) != 0;
    if (!f->mono && f->glyphs['i'].present && f->glyphs['M'].present)
        f->mono = fabsf(f->glyphs['i'].advance - f->glyphs['M'].advance) < 0.01f;

    /* Proportional fonts: quarter-pixel variants of ASCII for accurate spacing
       without blurring (each variant is still rasterized once, crisply). */
    if (!f->mono) {
        for (int q = 1; q < 4 && !full; ++q) {
            for (int c = 32; c < 128 && !full; ++c) {
                if (!f->glyphs[c].present) continue;
                int r = pack_glyph(f, face, f->glyphs[c].glyph_index, q * 16,
                                   &pk, &f->sub[q - 1][c]);
                if (r == PACK_FULL) full = true;
            }
        }
    }

    /* Then as much of the rest as fits: ligature/alternate glyphs, Latin-1,
       box drawing, ... Looked up by glyph id straight from HarfBuzz output. */
    int extra = 0;
    for (FT_UInt gi = 0; gi < f->by_index_count && !full && extra < MAX_EXTRA_GLYPHS; ++gi) {
        if (f->by_index[gi].present) continue;
        int r = pack_glyph(f, face, gi, 0, &pk, &f->by_index[gi]);
        if (r == PACK_FULL) full = true;
        else if (r == PACK_OK) ++extra;
    }
    if (full)
        fprintf(stderr, "font: atlas full, some glyphs not baked\n");
    return true;
}

/* ------------------------------------------------------------------------ */
/* GPU resources                                                             */
/* ------------------------------------------------------------------------ */

static bool gpu_upload_atlas(Font *f)
{
    VkContext *ctx = f->ctx;
    VkDevice dev = ctx->device;
    bool ok = false;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    const VkDeviceSize size = (VkDeviceSize)f->atlas_w * (VkDeviceSize)f->atlas_h;

    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = { (uint32_t)f->atlas_w, (uint32_t)f->atlas_h, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_TRY(vkCreateImage(dev, &ici, NULL, &f->image));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, f->image, &req);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_mem_type(ctx->physical_device, req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VK_TRY(vkAllocateMemory(dev, &mai, NULL, &f->memory));
    VK_TRY(vkBindImageMemory(dev, f->image, f->memory, 0));

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VK_TRY(vkCreateBuffer(dev, &bci, NULL, &staging));
    VkMemoryRequirements breq;
    vkGetBufferMemoryRequirements(dev, staging, &breq);
    VkMemoryAllocateInfo bmai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = breq.size,
        .memoryTypeIndex = find_mem_type(ctx->physical_device, breq.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    VK_TRY(vkAllocateMemory(dev, &bmai, NULL, &staging_mem));
    VK_TRY(vkBindBufferMemory(dev, staging, staging_mem, 0));
    void *mapped = NULL;
    VK_TRY(vkMapMemory(dev, staging_mem, 0, size, 0, &mapped));
    memcpy(mapped, f->atlas_pixels, (size_t)size);
    vkUnmapMemory(dev, staging_mem);

    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_TRY(vkAllocateCommandBuffers(dev, &cai, &cmd));
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_TRY(vkBeginCommandBuffer(cmd, &begin));

    VkImageMemoryBarrier to_dst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = f->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_dst);
    VkBufferImageCopy region = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { (uint32_t)f->atlas_w, (uint32_t)f->atlas_h, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging, f->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VkImageMemoryBarrier to_sample = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = f->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_sample);
    VK_TRY(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    VK_TRY(vkQueueSubmit(ctx->graphics_queue, 1, &submit, VK_NULL_HANDLE));
    VK_TRY(vkQueueWaitIdle(ctx->graphics_queue));

    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = f->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VK_TRY(vkCreateImageView(dev, &vci, NULL, &f->view));

    /* Quads map 1 texel : 1 pixel, so filtering is a no-op in the normal case;
       LINEAR only matters as a graceful fallback if the swapchain scale is off. */
    VkSamplerCreateInfo sci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    VK_TRY(vkCreateSampler(dev, &sci, NULL, &f->sampler));
    ok = true;

done:
    if (cmd) vkFreeCommandBuffers(dev, ctx->command_pool, 1, &cmd);
    if (staging) vkDestroyBuffer(dev, staging, NULL);
    if (staging_mem) vkFreeMemory(dev, staging_mem, NULL);
    return ok;
}

static bool gpu_create_ring(Font *f)
{
    VkDevice dev = f->ctx->device;
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = RING_BYTES,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_TRY(vkCreateBuffer(dev, &bci, NULL, &f->inst_buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, f->inst_buf, &req);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_mem_type(f->ctx->physical_device, req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    VK_TRY(vkAllocateMemory(dev, &mai, NULL, &f->inst_mem));
    VK_TRY(vkBindBufferMemory(dev, f->inst_buf, f->inst_mem, 0));
    VK_TRY(vkMapMemory(dev, f->inst_mem, 0, RING_BYTES, 0, &f->inst_map));
    f->inst_size = RING_BYTES;
    f->inst_head = 0;
    return true;

done:
    return false;
}

static bool gpu_create_pipeline(Font *f,
                                const uint32_t *vert_spv, size_t vert_n,
                                const uint32_t *frag_spv, size_t frag_n)
{
    VkContext *ctx = f->ctx;
    VkDevice dev = ctx->device;

    f->vert = make_module(dev, vert_spv, vert_n);
    f->frag = make_module(dev, frag_spv, frag_n);
    if (!f->vert || !f->frag) {
        fprintf(stderr, "font: shader module creation failed\n");
        return false;
    }

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
    VK_TRY(vkCreateDescriptorSetLayout(dev, &dsl, NULL, &f->set_layout));

    VkDescriptorPoolSize psz = { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 2 };
    VkDescriptorPoolCreateInfo dpi = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &psz,
    };
    VK_TRY(vkCreateDescriptorPool(dev, &dpi, NULL, &f->pool));
    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = f->pool, .descriptorSetCount = 1, .pSetLayouts = &f->set_layout,
    };
    VK_TRY(vkAllocateDescriptorSets(dev, &dsai, &f->set));
    VkDescriptorImageInfo di = {
        .sampler = f->sampler, .imageView = f->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = f->set, .dstBinding = 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &di,
    };
    vkUpdateDescriptorSets(dev, 1, &write, 0, NULL);

    VkPushConstantRange pcr = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(TextPush),
    };
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &f->set_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr,
    };
    VK_TRY(vkCreatePipelineLayout(dev, &plci, NULL, &f->layout));

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = f->vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = f->frag, .pName = "main" },
    };
    VkVertexInputBindingDescription vbind = {
        .binding = 0,
        .stride = sizeof(GlyphInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };
    VkVertexInputAttributeDescription vattr[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
          .offset = offsetof(GlyphInstance, rect) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
          .offset = offsetof(GlyphInstance, uv) },
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vbind,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = vattr,
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
        .pDynamicState = &dyn, .layout = f->layout,
        .renderPass = ctx->render_pass, .subpass = 0,
    };
    VK_TRY(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, NULL, &f->pipeline));
    return true;

done:
    return false;
}

/* ------------------------------------------------------------------------ */
/* Create / destroy                                                          */
/* ------------------------------------------------------------------------ */

bool font_create(Font *out, VkContext *ctx,
                 const char *ttf_path, float pixel_height, float dpi_scale,
                 const uint32_t *vert_spv, size_t vert_n,
                 const uint32_t *frag_spv, size_t frag_n)
{
    memset(out, 0, sizeof(*out));
    out->ctx = ctx;
    out->pixel_height = pixel_height;
    if (dpi_scale < 1.0f) dpi_scale = 1.0f;
    out->render_scale = dpi_scale;
    out->path = dup_string(ttf_path);

    FT_Library library = NULL;
    FT_Face face = NULL;
    if (FT_Init_FreeType(&library) != 0) {
        fprintf(stderr, "font: FT_Init_FreeType failed\n");
        goto fail;
    }
    out->ft_library = library;
    out->weight = 1.0f;
    out->contrast = 0.25f;
#if FONT_STEM_DARKENING
    {
        FT_Bool on = 0;   /* "no-stem-darkening" = false -> darkening enabled */
        FT_Property_Set(library, "cff", "no-stem-darkening", &on);
        FT_Property_Set(library, "autofit", "no-stem-darkening", &on);
        FT_Property_Set(library, "type1", "no-stem-darkening", &on);
        FT_Property_Set(library, "t1cid", "no-stem-darkening", &on);
    }
#endif
    if (FT_New_Face(library, ttf_path, 0, &face) != 0) {
        fprintf(stderr, "font: cannot open %s\n", ttf_path);
        goto fail;
    }
    out->ft_face = face;

    /* Rasterize at integer physical pixels (logical size x DPI). Integer ppem
       keeps the hinter's vertical snapping (x-height, baseline, caps) exact. */
    float physical = pixel_height * dpi_scale;
    int px = (int)(physical + 0.5f);
    if (px < MIN_PIXEL_SIZE) px = MIN_PIXEL_SIZE;
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) != 0) {
        fprintf(stderr, "font: FT_Set_Pixel_Sizes failed\n");
        goto fail;
    }

    /* Metrics in *logical* units so layout matches UI coordinates. FreeType
       already rounds these to whole device pixels for scalable fonts. */
    const float inv = 1.0f / dpi_scale;
    const FT_Size_Metrics *m = &face->size->metrics;
    out->ascent      = ((float)m->ascender  / 64.0f) * inv;
    out->descent     = ((float)m->descender / 64.0f) * inv;
    out->line_height = ((float)m->height    / 64.0f) * inv;
    if (out->line_height < 1.0f)
        out->line_height = out->ascent - out->descent;
    out->line_gap = out->line_height - (out->ascent - out->descent);
    if (out->line_gap < 0.0f) out->line_gap = 0.0f;

    if (!bake_atlas(out, face))
        goto fail;

    out->space_advance = out->glyphs[' '].present
                       ? out->glyphs[' '].advance
                       : (float)px * inv * 0.5f;

    /* Keep FT face alive for HarfBuzz; same load flags as the atlas so shaping
       advances agree with the rasterized glyphs. */
    out->hb_font = hb_ft_font_create(face, NULL);
    if (out->hb_font)
        hb_ft_font_set_load_flags((hb_font_t *)out->hb_font,
                                  FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP);

    if (!gpu_upload_atlas(out)) goto fail;
    if (!gpu_create_ring(out)) goto fail;
    if (!gpu_create_pipeline(out, vert_spv, vert_n, frag_spv, frag_n)) goto fail;

    out->ready = true;
    printf("Font ready (FT light + HB): %s  logical %.1fpx  raster %dpx  scale %.2f  %s\n",
           ttf_path, pixel_height, px, dpi_scale, out->mono ? "mono" : "proportional");
    return true;

fail:
    /* font_destroy tolerates a half-built Font; make sure it owns the handles. */
    if (!out->ft_face && face) out->ft_face = face;
    if (!out->ft_library && library) out->ft_library = library;
    font_destroy(out);
    return false;
}

void font_destroy(Font *f)
{
    if (!f) return;
    if (f->hb_font) hb_font_destroy((hb_font_t *)f->hb_font);
    if (f->ft_face) FT_Done_Face((FT_Face)f->ft_face);
    if (f->ft_library) FT_Done_FreeType((FT_Library)f->ft_library);

    VkDevice dev = f->ctx ? f->ctx->device : VK_NULL_HANDLE;
    if (dev) {
        vkDeviceWaitIdle(dev);
        if (f->inst_map) vkUnmapMemory(dev, f->inst_mem);
        if (f->inst_buf) vkDestroyBuffer(dev, f->inst_buf, NULL);
        if (f->inst_mem) vkFreeMemory(dev, f->inst_mem, NULL);
        if (f->pipeline) vkDestroyPipeline(dev, f->pipeline, NULL);
        if (f->layout) vkDestroyPipelineLayout(dev, f->layout, NULL);
        if (f->pool) vkDestroyDescriptorPool(dev, f->pool, NULL);
        if (f->set_layout) vkDestroyDescriptorSetLayout(dev, f->set_layout, NULL);
        if (f->vert) vkDestroyShaderModule(dev, f->vert, NULL);
        if (f->frag) vkDestroyShaderModule(dev, f->frag, NULL);
        if (f->sampler) vkDestroySampler(dev, f->sampler, NULL);
        if (f->view) vkDestroyImageView(dev, f->view, NULL);
        if (f->image) vkDestroyImage(dev, f->image, NULL);
        if (f->memory) vkFreeMemory(dev, f->memory, NULL);
    }
    free(f->atlas_pixels);
    free(f->by_index);
    free(f->path);
    memset(f, 0, sizeof(*f));
}

void font_set_tuning(Font *f, float weight, float contrast)
{
    if (!f) return;
    if (weight < 0.25f) weight = 0.25f;
    if (weight > 4.0f)  weight = 4.0f;
    if (contrast < 0.0f) contrast = 0.0f;
    if (contrast > 1.0f) contrast = 1.0f;
    f->weight = weight;
    f->contrast = contrast;
}

/* ------------------------------------------------------------------------ */
/* Layout                                                                    */
/*                                                                           */
/* Pen math runs in *device* pixels. Fixed-width fonts get integer advances  */
/* (even column grid); proportional fonts keep fractional advances so total  */
/* width stays right. Either way each glyph origin is rounded to a whole     */
/* pixel at draw time. font_measure() and font_draw() share this logic.      */
/* ------------------------------------------------------------------------ */

static inline float adv_device(const Font *f, float adv_dev)
{
    return f->mono ? roundf(adv_dev) : adv_dev;
}

static bool is_ascii(const char *s)
{
    for (; *s; ++s)
        if ((unsigned char)*s >= 0x80) return false;
    return true;
}

static hb_buffer_t *shape_text(const Font *f, const char *text)
{
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);
    hb_shape((hb_font_t *)f->hb_font, buf, SHAPE_FEATURES, SHAPE_N_FEATURES);
    return buf;
}

float font_measure(const Font *f, const char *text)
{
    if (!f || !text) return 0.f;
    const float s = f->render_scale;
    float w = 0.f;

    /* Fast path: fixed-width + ASCII needs no shaping (no kerning in mono). */
    if (!f->hb_font || (f->mono && is_ascii(text))) {
        for (const char *p = text; *p; ++p) {
            unsigned char c = (unsigned char)*p;
            if (c == '\n') continue;
            if (c < 32 || c > 126 || !f->glyphs[c].present)
                w += adv_device(f, f->space_advance * s);
            else
                w += adv_device(f, f->glyphs[c].advance * s);
        }
        return w / s;
    }

    hb_buffer_t *buf = shape_text(f, text);
    unsigned int count = 0;
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);
    for (unsigned int i = 0; i < count; ++i) {
        if (text[info[i].cluster] == '\n') continue;
        w += adv_device(f, (float)pos[i].x_advance / 64.0f);
    }
    hb_buffer_destroy(buf);
    return w / s;
}

/* ------------------------------------------------------------------------ */
/* Batching: glyph quads are written straight into the mapped instance ring   */
/* and drawn with one vkCmdDraw(6, n) per sub-batch (normally one per call).  */
/* ------------------------------------------------------------------------ */

typedef struct Batch {
    Font *f;
    VkCommandBuffer cmd;
    GlyphInstance *dst;
    uint32_t n;
    VkDeviceSize offset;
    float sw, sh;
} Batch;

static void batch_reserve(Batch *b)
{
    Font *f = b->f;
    const VkDeviceSize need = (VkDeviceSize)BATCH_MAX * sizeof(GlyphInstance);
    if (f->inst_head + need > f->inst_size)
        f->inst_head = 0;                       /* wrap the ring */
    b->offset = f->inst_head;
    b->dst = (GlyphInstance *)((char *)f->inst_map + b->offset);
    b->n = 0;
}

static void batch_flush(Batch *b)
{
    if (b->n == 0) return;
    vkCmdBindVertexBuffers(b->cmd, 0, 1, &b->f->inst_buf, &b->offset);
    vkCmdDraw(b->cmd, 6, b->n, 0, 0);
    b->f->inst_head = b->offset + (VkDeviceSize)b->n * sizeof(GlyphInstance);
    b->n = 0;
}

/* Queue one glyph. ox/oy are the glyph origin (pen/baseline) in whole device
   pixels; bitmap offsets and size are whole device pixels too. */
static void batch_add(Batch *b, const FontGlyph *g, float ox, float oy)
{
    const float s = b->f->render_scale;
    const float w = roundf((g->x1 - g->x0) * s);
    const float h = roundf((g->y1 - g->y0) * s);
    if (w <= 0.f || h <= 0.f) return;

    const float x = ox + roundf(g->x0 * s);
    const float y = oy + roundf(g->y0 * s);
    if (x + w < 0.f || y + h < 0.f || x > b->sw || y > b->sh) return;   /* off-screen */

    if (b->n == BATCH_MAX) {
        batch_flush(b);
        batch_reserve(b);
    }
    GlyphInstance *gi = &b->dst[b->n++];
    gi->rect[0] = x;     gi->rect[1] = y;     gi->rect[2] = w;     gi->rect[3] = h;
    gi->uv[0]   = g->u0; gi->uv[1]   = g->v0; gi->uv[2]   = g->u1; gi->uv[3]   = g->v1;
}

void font_draw(Font *f, VkContext *ctx,
               float x, float baseline_y,
               const char *text, const uint8_t rgba[4])
{
    if (!f || !f->ready || !ctx || !ctx->frame_open || !text) return;

    VkCommandBuffer cmd = ctx->active_cmd;
    VkViewport viewport = {
        .width = (float)ctx->swapchain_extent.width,
        .height = (float)ctx->swapchain_extent.height,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { .extent = ctx->swapchain_extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, f->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, f->layout, 0, 1, &f->set, 0, NULL);

    const float s = f->render_scale;
    TextPush pc = {
        .color  = { rgba[0] / 255.f, rgba[1] / 255.f, rgba[2] / 255.f, rgba[3] / 255.f },
        .screen = { (float)ctx->swapchain_extent.width, (float)ctx->swapchain_extent.height },
        .tune   = { f->weight, f->contrast },
    };
    vkCmdPushConstants(cmd, f->layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    Batch batch = { .f = f, .cmd = cmd, .sw = pc.screen[0], .sh = pc.screen[1] };
    batch_reserve(&batch);

    const float x_dev = x * s;                 /* line start, device px */
    const float line_dev = roundf(f->line_height * s);
    float pen_x = x_dev;
    float pen_y = roundf(baseline_y * s);      /* baseline always on a pixel row */

    /* Fast path: fixed-width + ASCII, straight from the atlas cache. */
    if (!f->hb_font || (f->mono && is_ascii(text))) {
        for (const char *p = text; *p; ++p) {
            unsigned char c = (unsigned char)*p;
            if (c == '\n') { pen_x = x_dev; pen_y += line_dev; continue; }
            if (c < 32 || c > 126 || !f->glyphs[c].present) {
                pen_x += adv_device(f, f->space_advance * s);
                continue;
            }
            const FontGlyph *g = &f->glyphs[c];
            batch_add(&batch, g, roundf(pen_x), pen_y);
            pen_x += adv_device(f, g->advance * s);
        }
        batch_flush(&batch);
        return;
    }

    /* General path: HarfBuzz shaping (kerning, proportional fonts, non-ASCII). */
    hb_buffer_t *buf = shape_text(f, text);
    unsigned int count = 0;
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);

    for (unsigned int i = 0; i < count; ++i) {
        unsigned char src = (unsigned char)text[info[i].cluster];
        if (src == '\n') { pen_x = x_dev; pen_y += line_dev; continue; }

        unsigned int gid = info[i].codepoint;
        if (src >= 32 && gid < f->by_index_count && f->by_index[gid].present) {
            const float ox = pen_x + (float)pos[i].x_offset / 64.0f;
            const float oy = roundf(pen_y - (float)pos[i].y_offset / 64.0f);
            const FontGlyph *g = &f->by_index[gid];
            float ix = roundf(ox);

            /* Proportional ASCII: pick the nearest quarter-pixel variant. */
            if (!f->mono && src < 128 && f->glyphs[src].present &&
                f->glyphs[src].glyph_index == gid) {
                ix = floorf(ox);
                int q = (int)((ox - ix) * 4.0f + 0.5f);
                if (q >= 4) { ix += 1.0f; q = 0; }
                if (q > 0 && f->sub[q - 1][src].present)
                    g = &f->sub[q - 1][src];
                else if (q > 0)
                    ix = roundf(ox);
            }
            batch_add(&batch, g, ix, oy);
        }
        pen_x += adv_device(f, (float)pos[i].x_advance / 64.0f);
        pen_y -= (float)pos[i].y_advance / 64.0f;   /* HB y-up -> screen y-down */
    }
    hb_buffer_destroy(buf);
    batch_flush(&batch);
}
