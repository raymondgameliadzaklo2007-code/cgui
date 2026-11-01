/**
 * font.h – High-quality text (FreeType + HarfBuzz)
 *
 * Practical Core Text *approximation* for an editor:
 * - FreeType light hinting (vertical-only snapping) at DPI-scaled integer sizes
 * - HarfBuzz shaping (kerning / advances), ligatures off by default (see font.c)
 * - Every quad lands on whole device pixels, 1 atlas texel == 1 device pixel
 * - Quarter-pixel positioned glyphs for proportional fonts
 * - Batched: one instanced draw per font_draw() call (no per-glyph draws)
 * - FreeType stem darkening on by default (-DFONT_STEM_DARKENING=0 to disable)
 * - Luminance-aware gamma coverage in the fragment shader, tunable at runtime
 *
 * Coordinate contract (unchanged for callers):
 *   - x / baseline_y passed to font_draw(), and everything font_measure()
 *     and the Font metrics return, are in *logical* UI units.
 *   - Device pixels = logical * render_scale. The swapchain extent must be the
 *     physical framebuffer size.
 *
 * Not a Core Text clone (impossible / proprietary).
 */

#ifndef FONT_H
#define FONT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "vk_context.h"

typedef struct FontGlyph {
    float advance;            /* logical units */
    float x0, y0, x1, y1;     /* bitmap box relative to pen/baseline, logical */
    float u0, v0, u1, v1;
    unsigned int glyph_index; /* FreeType / HB index */
    bool present;
} FontGlyph;

typedef struct Font {
    float pixel_height;  /* logical size */
    float render_scale;  /* DPI scale used when rasterizing */
    float ascent;
    float descent;       /* negative, below the baseline */
    float line_gap;
    float line_height;
    float space_advance;

    int atlas_w, atlas_h;
    unsigned char *atlas_pixels;

    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;

    VkShaderModule vert, frag;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;

    /* Instance ring: one persistently mapped vertex buffer; each font_draw()
       writes its glyph quads here and issues a single instanced draw. */
    VkBuffer inst_buf;
    VkDeviceMemory inst_mem;
    void *inst_map;
    VkDeviceSize inst_size, inst_head;

    /* ASCII cache for fast code path */
    FontGlyph glyphs[128];

    /* Every baked glyph, indexed by glyph id (HarfBuzz output). Covers ASCII,
       ligature/alternate glyphs and as much of the rest of the font as fits. */
    FontGlyph *by_index;
    unsigned int by_index_count;

    /* FreeType + HarfBuzz (opaque to keep headers light) */
    void *ft_library;
    void *ft_face;
    void *hb_face;
    void *hb_font;

    bool mono;           /* fixed-width: integer device-pixel advances */

    /* Quarter-pixel horizontal variants of ASCII (1/4, 2/4, 3/4 px) used for
       proportional fonts only; fixed-width fonts stay on the integer grid. */
    FontGlyph sub[3][128];

    /* Runtime tuning, see font_set_tuning(). */
    float weight;        /* 1 = default, >1 bolder, <1 lighter */
    float contrast;      /* 0..1 edge steepening */

    char *path;
    VkContext *ctx;
    bool ready;
} Font;

bool font_create(Font *out, VkContext *ctx,
                 const char *ttf_path, float pixel_height, float dpi_scale,
                 const uint32_t *vert_spv, size_t vert_n,
                 const uint32_t *frag_spv, size_t frag_n);

void font_destroy(Font *f);

/* Adjust rendering weight/contrast at runtime (no re-bake, takes effect on the
   next draw). Defaults: weight 1.0, contrast 0.25. Values are clamped. */
void  font_set_tuning(Font *f, float weight, float contrast);

float font_measure(const Font *f, const char *text);
void  font_draw(Font *f, VkContext *ctx,
                float x, float baseline_y,
                const char *text, const uint8_t rgba[4]);

#endif /* FONT_H */
