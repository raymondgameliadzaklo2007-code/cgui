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
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_OUTLINE_H
#include FT_FONT_FORMATS_H

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

/* At or above this raster size glyphs are rendered unhinted at the exact
   fractional size: hinting only helps when stems are a few pixels wide, and at
   large sizes it distorts shapes. Override with -DFONT_HINT_MAX_PPEM=<px>. */
#ifndef FONT_HINT_MAX_PPEM
#define FONT_HINT_MAX_PPEM  40.0f
#endif

#define ATLAS_MIN           1024
#define ATLAS_MAX           4096   /* guaranteed by every Vulkan implementation */
#define ATLAS_PAD           2      /* gap between glyphs in the atlas */
#ifndef MAX_EXTRA_GLYPHS
#define MAX_EXTRA_GLYPHS    3000   /* non-ASCII glyphs baked at startup */
#endif
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

/* Per-draw constants (56 bytes). Layout must match text.vert / text.frag. */
typedef struct TextPush {
    float color[4];
    float tune[4];      /* weight, contrast, srgb target, raster ppem */
    float bg[4];        /* background rgb + known flag */
    float screen[2];
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

/* defined below; used by font_create for env overrides */
void font_set_tuning(Font *f, float weight, float contrast);

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

typedef struct UnicodeRange {
    uint32_t start;
    uint32_t end;
} UnicodeRange;

/* Priority Unicode codepoint ranges for Nerd Font icons and terminal symbols */
static const UnicodeRange s_priority_ranges[] = {
    { 0x2500, 0x259F },   /* Box Drawing & Block Elements */
    { 0x2190, 0x21FF },   /* Arrows */
    { 0x2300, 0x23FF },   /* Miscellaneous Technical */
    { 0x2600, 0x26FF },   /* Miscellaneous Symbols */
    { 0x2700, 0x27BF },   /* Dingbats */
    { 0x2B00, 0x2BFF },   /* Miscellaneous Symbols and Arrows */
    { 0xE000, 0xF8FF },   /* Private Use Area (Nerd Font Core: Devicons, FontAwesome, etc.) */
    { 0xE0A0, 0xE0D4 },   /* Powerline symbols & extra */
    { 0xEA60, 0xEBEB },   /* VS Code Codicons */
    { 0xF000, 0xF2E0 },   /* Font Awesome / Octicons */
    { 0xF0000, 0xF1AF0 }, /* Material Design Icons (Nerd Font v3 PUA) */
};
#define NUM_PRIORITY_RANGES (sizeof(s_priority_ranges) / sizeof(s_priority_ranges[0]))

static uint32_t utf8_decode_char(const char *p, size_t *out_len)
{
    if (!p || !*p) {
        if (out_len) *out_len = 0;
        return 0;
    }
    const unsigned char *s = (const unsigned char *)p;
    if (s[0] < 0x80) {
        if (out_len) *out_len = 1;
        return s[0];
    } else if ((s[0] & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) == 0x80) {
            if (out_len) *out_len = 2;
            return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        }
    } else if ((s[0] & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
            if (out_len) *out_len = 3;
            return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        }
    } else if ((s[0] & 0xF8) == 0xF0) {
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
            if (out_len) *out_len = 4;
            return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        }
    }
    if (out_len) *out_len = 1;
    return s[0];
}

static const char *find_nerd_font_fallback(void)
{
    static const char *candidates[] = {
        "assets/fonts/SymbolsNerdFontMono-Regular.ttf",
        "assets/fonts/SymbolsNerdFont-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (access(candidates[i], R_OK) == 0) {
            return candidates[i];
        }
    }
    return NULL;
}


/* Rasterize one glyph with light hinting and shelf-pack it into the atlas. */
static int pack_glyph(Font *f, FT_Face face, FT_UInt gindex, int shift26,
                      Packer *pk, FontGlyph *fg)
{
    if (FT_Load_Glyph(face, gindex, (FT_Int32)f->load_flags) != 0)
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
    /* Size atlas to fit ASCII, Nerd font glyphs, and fallback symbols.
       2048x2048 provides ample room (~10,000 glyphs at typical coding ppem). */
    int side = 2048;
    f->atlas_w = f->atlas_h = side;
    f->atlas_pixels = calloc((size_t)f->atlas_w * (size_t)f->atlas_h, 1);
    f->by_index_count = (unsigned int)face->num_glyphs;
    f->by_index = calloc(f->by_index_count ? f->by_index_count : 1, sizeof(FontGlyph));
    if (!f->atlas_pixels || !f->by_index)
        return false;

    Packer pk = { ATLAS_PAD, ATLAS_PAD, 0 };
    bool full = false;

    /* 1. ASCII first so it is guaranteed to fit. */
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

    /* Proportional fonts: quarter-pixel variants of ASCII for accurate spacing. */
    const bool large = f->ppem >= FONT_HINT_MAX_PPEM;
    if (!f->mono && !large) {
        for (int q = 1; q < 4 && !full; ++q) {
            for (int c = 32; c < 128 && !full; ++c) {
                if (!f->glyphs[c].present) continue;
                int r = pack_glyph(f, face, f->glyphs[c].glyph_index, q * 16,
                                   &pk, &f->sub[q - 1][c]);
                if (r == PACK_FULL) full = true;
            }
        }
    }

    /* 2. Priority Unicode ranges: Nerd Font icons & terminal symbols in primary face */
    for (size_t r_idx = 0; r_idx < NUM_PRIORITY_RANGES && !full; ++r_idx) {
        uint32_t start = s_priority_ranges[r_idx].start;
        uint32_t end = s_priority_ranges[r_idx].end;
        for (uint32_t cp = start; cp <= end && !full; ++cp) {
            FT_UInt gi = FT_Get_Char_Index(face, (FT_ULong)cp);
            if (gi == 0 || gi >= f->by_index_count || f->by_index[gi].present)
                continue;
            int r = pack_glyph(f, face, gi, 0, &pk, &f->by_index[gi]);
            if (r == PACK_FULL) full = true;
        }
    }

    /* 3. Priority Unicode ranges in fallback face if primary face misses them */
    if (f->fallback_face && !full) {
        FT_Face fb_face = (FT_Face)f->fallback_face;
        f->fallback_by_index_count = (unsigned int)fb_face->num_glyphs;
        f->fallback_by_index = calloc(f->fallback_by_index_count ? f->fallback_by_index_count : 1, sizeof(FontGlyph));
        if (f->fallback_by_index) {
            f->has_fallback = true;
            for (size_t r_idx = 0; r_idx < NUM_PRIORITY_RANGES && !full; ++r_idx) {
                uint32_t start = s_priority_ranges[r_idx].start;
                uint32_t end = s_priority_ranges[r_idx].end;
                for (uint32_t cp = start; cp <= end && !full; ++cp) {
                    FT_UInt fb_gi = FT_Get_Char_Index(fb_face, (FT_ULong)cp);
                    if (fb_gi == 0 || fb_gi >= f->fallback_by_index_count || f->fallback_by_index[fb_gi].present)
                        continue;
                    int r = pack_glyph(f, fb_face, fb_gi, 0, &pk, &f->fallback_by_index[fb_gi]);
                    if (r == PACK_FULL) full = true;
                }
            }
        }
    }

    /* 4. Pack additional glyphs from primary font up to capacity */
    for (FT_UInt gi = 0; gi < f->by_index_count && !full; ++gi) {
        if (f->by_index[gi].present) continue;
        int r = pack_glyph(f, face, gi, 0, &pk, &f->by_index[gi]);
        if (r == PACK_FULL) full = true;
    }

    for (int c = 32; c < 127; ++c) {
        if (!f->glyphs[c].present && FT_Get_Char_Index(face, (FT_ULong)c)) {
            fprintf(stderr, "font: atlas too small, ASCII glyphs missing (size %d)\n", f->atlas_w);
            break;
        }
    }
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
#if FONT_STEM_DARKENING
    {
        FT_Bool on = 0;   /* "no-stem-darkening" = false -> darkening enabled */
        FT_Property_Set(library, "cff", "no-stem-darkening", &on);
        FT_Property_Set(library, "autofitter", "no-stem-darkening", &on);
        FT_Property_Set(library, "type1", "no-stem-darkening", &on);
        FT_Property_Set(library, "t1cid", "no-stem-darkening", &on);
    }
#endif
    if (FT_New_Face(library, ttf_path, 0, &face) != 0) {
        fprintf(stderr, "font: cannot open %s\n", ttf_path);
        goto fail;
    }
    out->ft_face = face;

    /* Default tuning. FreeType's stem darkening thickens CFF/OpenType outlines
       noticeably more than TrueType ones (measured ~+25% vs ~+17% ink at small
       sizes), so those start a little lighter to land at the same visual weight. */
    out->weight = 1.0f;
    out->contrast = 0.25f;
#if FONT_STEM_DARKENING
    {
        const char *fmt = FT_Get_Font_Format(face);
        if (fmt && (strcmp(fmt, "CFF") == 0 || strcmp(fmt, "Type 1") == 0 ||
                    strcmp(fmt, "CID Type 1") == 0))
            out->weight = 0.82f;
    }
#endif
    /* Quick experiments without touching the editor:
         FONT_WEIGHT=1.1 FONT_CONTRAST=0.3 FONT_SRGB=1 ./editor */
    {
        const char *e;
        float wv = out->weight, cv = out->contrast;
        if ((e = getenv("FONT_WEIGHT")))   wv = (float)atof(e);
        if ((e = getenv("FONT_CONTRAST"))) cv = (float)atof(e);
        font_set_tuning(out, wv, cv);
        if ((e = getenv("FONT_SRGB")))     out->target_srgb = atoi(e) != 0;
    }

    /* Rasterize at integer physical pixels (logical size x DPI). Integer ppem
       keeps the hinter's vertical snapping (x-height, baseline, caps) exact. */
    float physical = pixel_height * dpi_scale;
    int px = (int)(physical + 0.5f);
    if (px < MIN_PIXEL_SIZE) px = MIN_PIXEL_SIZE;
    if ((float)px >= FONT_HINT_MAX_PPEM) {
        /* Large: exact fractional size, no hinting (72 dpi => 1 pt == 1 px). */
        out->ppem = physical;
        out->load_flags = FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;
        if (FT_Set_Char_Size(face, 0, (FT_F26Dot6)(physical * 64.0f + 0.5f), 72, 72) != 0) {
            fprintf(stderr, "font: FT_Set_Char_Size failed\n");
            goto fail;
        }
    } else {
        /* Small: integer ppem keeps the light hinter's vertical snapping exact. */
        out->ppem = (float)px;
        out->load_flags = FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP;
        if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) != 0) {
            fprintf(stderr, "font: FT_Set_Pixel_Sizes failed\n");
            goto fail;
        }
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

    /* Load fallback symbols / Nerd Font face if available */
    const char *fallback_path = find_nerd_font_fallback();
    if (fallback_path) {
        FT_Face fb_face = NULL;
        if (FT_New_Face(library, fallback_path, 0, &fb_face) == 0) {
            if ((float)px >= FONT_HINT_MAX_PPEM) {
                FT_Set_Char_Size(fb_face, 0, (FT_F26Dot6)(physical * 64.0f + 0.5f), 72, 72);
            } else {
                FT_Set_Pixel_Sizes(fb_face, 0, (FT_UInt)px);
            }
            out->fallback_face = fb_face;
        }
    }

    if (!bake_atlas(out, face))
        goto fail;

    out->space_advance = out->glyphs[' '].present
                       ? out->glyphs[' '].advance
                       : (float)px * inv * 0.5f;

    /* Keep FT face alive for HarfBuzz; same load flags as the atlas so shaping
       advances agree with the rasterized glyphs. */
    out->hb_font = hb_ft_font_create(face, NULL);
    if (out->hb_font)
        hb_ft_font_set_load_flags((hb_font_t *)out->hb_font, out->load_flags);
    out->hb_buf = hb_buffer_create();
    if (!out->hb_buf && out->hb_font) {          /* no scratch buffer -> no HB path */
        hb_font_destroy((hb_font_t *)out->hb_font);
        out->hb_font = NULL;
    }

    if (!gpu_upload_atlas(out)) goto fail;
    if (!gpu_create_ring(out)) goto fail;
    if (!gpu_create_pipeline(out, vert_spv, vert_n, frag_spv, frag_n)) goto fail;

    out->ready = true;
    printf("Font ready (FT light + HB): %s  logical %.1fpx  raster %dpx  scale %.2f  %s (fallback: %s)\n",
           ttf_path, pixel_height, px, dpi_scale, out->mono ? "mono" : "proportional",
           out->has_fallback ? fallback_path : "none");
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
    if (f->hb_buf) hb_buffer_destroy((hb_buffer_t *)f->hb_buf);
    if (f->hb_font) hb_font_destroy((hb_font_t *)f->hb_font);
    if (f->fallback_face) FT_Done_Face((FT_Face)f->fallback_face);
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
    free(f->fallback_by_index);
    free(f->path);
    memset(f, 0, sizeof(*f));
}

void font_set_background(Font *f, const uint8_t rgb[3])
{
    if (!f) return;
    if (!rgb) { memset(f->bg, 0, sizeof(f->bg)); return; }
    f->bg[0] = rgb[0] / 255.0f;
    f->bg[1] = rgb[1] / 255.0f;
    f->bg[2] = rgb[2] / 255.0f;
    f->bg[3] = 1.0f;
}

void font_set_target_srgb(Font *f, bool srgb)
{
    if (f) f->target_srgb = srgb;
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

static size_t text_len(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) ++n;
    return n;
}

static bool is_ascii(const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if ((unsigned char)s[i] >= 0x80) return false;
    return true;
}

/* Shapes into the Font's reusable scratch buffer (owned by Font; do not destroy).
   One shaped run is alive at a time: measure and draw never nest. */
static hb_buffer_t *shape_text(const Font *f, const char *text, size_t n)
{
    hb_buffer_t *buf = (hb_buffer_t *)f->hb_buf;
    hb_buffer_clear_contents(buf);
    hb_buffer_add_utf8(buf, text, (int)n, 0, (int)n);
    hb_buffer_guess_segment_properties(buf);
    hb_shape((hb_font_t *)f->hb_font, buf, SHAPE_FEATURES, SHAPE_N_FEATURES);
    return buf;
}

float font_measure_n(const Font *f, const char *text, size_t n)
{
    if (!f || !text) return 0.f;
    n = text_len(text, n);
    const float s = f->render_scale;
    float w = 0.f;

    /* Fast path: fixed-width + ASCII needs no shaping (no kerning in mono). */
    if (!f->hb_font || (f->mono && is_ascii(text, n))) {
        for (size_t i = 0; i < n; ++i) {
            unsigned char c = (unsigned char)text[i];
            if (c == '\n') continue;
            if (c < 32 || c > 126 || !f->glyphs[c].present)
                w += adv_device(f, f->space_advance * s);
            else
                w += adv_device(f, f->glyphs[c].advance * s);
        }
        return w / s;
    }

    hb_buffer_t *buf = shape_text(f, text, n);
    unsigned int count = 0;
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);
    for (unsigned int i = 0; i < count; ++i) {
        if (text[info[i].cluster] == '\n') continue;
        float adv = (float)pos[i].x_advance / 64.0f;
        if (pos[i].x_advance == 0 && f->has_fallback && f->fallback_face) {
            uint32_t cp = utf8_decode_char(&text[info[i].cluster], NULL);
            FT_UInt fb_gi = FT_Get_Char_Index((FT_Face)f->fallback_face, (FT_ULong)cp);
            if (fb_gi > 0 && fb_gi < f->fallback_by_index_count && f->fallback_by_index[fb_gi].present) {
                adv = f->fallback_by_index[fb_gi].advance * s;
            }
        }
        w += adv_device(f, adv);
    }
    return w / s;
}

float font_measure(const Font *f, const char *text)
{
    return font_measure_n(f, text, (size_t)-1);
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
        .bg     = { f->bg[0], f->bg[1], f->bg[2], f->bg[3] },
        .screen = { (float)ctx->swapchain_extent.width, (float)ctx->swapchain_extent.height },
        .tune   = { f->weight, f->contrast, f->target_srgb ? 1.0f : 0.0f, f->ppem },
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
    const size_t len = strlen(text);

    /* Fast path: fixed-width + ASCII, straight from the atlas cache. */
    if (!f->hb_font || (f->mono && is_ascii(text, len))) {
        for (size_t i = 0; i < len; ++i) {
            unsigned char c = (unsigned char)text[i];
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
    hb_buffer_t *buf = shape_text(f, text, len);
    unsigned int count = 0;
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);

    for (unsigned int i = 0; i < count; ++i) {
        unsigned char src = (unsigned char)text[info[i].cluster];
        if (src == '\n') { pen_x = x_dev; pen_y += line_dev; continue; }

        unsigned int gid = info[i].codepoint;
        const FontGlyph *g = NULL;

        if (src >= 32 && gid < f->by_index_count && f->by_index[gid].present) {
            g = &f->by_index[gid];
        } else if (f->has_fallback && f->fallback_face) {
            /* Try fallback font */
            uint32_t cp = utf8_decode_char(&text[info[i].cluster], NULL);
            FT_UInt fb_gi = FT_Get_Char_Index((FT_Face)f->fallback_face, (FT_ULong)cp);
            if (fb_gi > 0 && fb_gi < f->fallback_by_index_count && f->fallback_by_index[fb_gi].present) {
                g = &f->fallback_by_index[fb_gi];
            }
        }

        float x_advance = (float)pos[i].x_advance / 64.0f;
        if (g) {
            const float ox = pen_x + (float)pos[i].x_offset / 64.0f;
            const float oy = roundf(pen_y - (float)pos[i].y_offset / 64.0f);
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
            if (x_advance <= 0.0f) {
                x_advance = g->advance * s;
            }
        }
        pen_x += adv_device(f, x_advance);
        pen_y -= (float)pos[i].y_advance / 64.0f;   /* HB y-up -> screen y-down */
    }
    batch_flush(&batch);
}
