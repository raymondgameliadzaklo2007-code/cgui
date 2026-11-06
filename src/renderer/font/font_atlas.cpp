#include "font_atlas.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace cgui::font {

FontAtlas::FontAtlas() {
    generate_default_atlas();
}

const Glyph* FontAtlas::get_glyph(uint32_t codepoint) const noexcept {
    auto it = glyphs_.find(codepoint);
    if (it != glyphs_.end()) {
        return &it->second;
    }
    // Fallback to space or question mark
    it = glyphs_.find(static_cast<uint32_t>('?'));
    if (it != glyphs_.end()) {
        return &it->second;
    }
    it = glyphs_.find(static_cast<uint32_t>(' '));
    if (it != glyphs_.end()) {
        return &it->second;
    }
    return nullptr;
}

float FontAtlas::measure_text_width(std::string_view text, float font_size) const noexcept {
    float width = 0.0f;
    for (char c : text) {
        if (c == '\n') continue;
        const auto* glyph = get_glyph(static_cast<uint32_t>(c));
        if (glyph) {
            width += glyph->advance * font_size;
        } else {
            width += 0.5f * font_size;
        }
    }
    return width;
}

float FontAtlas::measure_text_height(float font_size) const noexcept {
    return line_height_ * font_size;
}

namespace {

// Clean procedural MSDF distance calculation for vector stroke primitives
float dist_to_segment(float px, float py, float ax, float ay, float bx, float by) {
    float l2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
    if (l2 == 0.0f) return std::hypot(px - ax, py - ay);
    float t = std::clamp(((px - ax) * (bx - ax) + (py - ay) * (by - ay)) / l2, 0.0f, 1.0f);
    float proj_x = ax + t * (bx - ax);
    float proj_y = ay + t * (by - ay);
    return std::hypot(px - proj_x, py - proj_y);
}

// Distance to axis-aligned box
float dist_to_box(float px, float py, float x0, float y0, float x1, float y1) {
    float cx = 0.5f * (x0 + x1);
    float cy = 0.5f * (y0 + y1);
    float hw = 0.5f * (x1 - x0);
    float hh = 0.5f * (y1 - y0);
    float dx = std::abs(px - cx) - hw;
    float dy = std::abs(py - cy) - hh;
    float ox = std::max(dx, 0.0f);
    float oy = std::max(dy, 0.0f);
    float out_dist = std::hypot(ox, oy);
    float in_dist = std::min(std::max(dx, dy), 0.0f);
    return out_dist + in_dist;
}

} // anonymous namespace

void FontAtlas::generate_default_atlas() {
    width_ = 512;
    height_ = 512;
    pixels_.assign(width_ * height_ * 4, 0);

    // Cell grid for ASCII 32..126 (95 characters)
    const int cols = 16;
    const int rows = 8;
    const float cell_w = static_cast<float>(width_) / static_cast<float>(cols);  // 32 px
    const float cell_h = static_cast<float>(height_) / static_cast<float>(rows); // 64 px
    const float em_scale = cell_h * 0.65f; // font em size in pixels

    px_range_ = 4.0f;
    line_height_ = 1.30f;
    ascender_ = 0.85f;
    descender_ = -0.25f;

    for (int idx = 0; idx < 95; ++idx) {
        char ch = static_cast<char>(32 + idx);
        int col = idx % cols;
        int row = idx / cols;

        float origin_x = col * cell_w;
        float origin_y = row * cell_h;

        Glyph glyph{};
        glyph.codepoint = static_cast<uint32_t>(ch);

        // Proportional character widths
        float adv = 0.55f;
        if (ch == ' ' || ch == '.' || ch == ':' || ch == ';' || ch == '!' || ch == '|' || ch == '\'' || ch == 'i' || ch == 'l') {
            adv = 0.32f;
        } else if (ch == 'm' || ch == 'w' || ch == 'M' || ch == 'W' || ch == '@' || ch == '%') {
            adv = 0.85f;
        } else if (ch >= 'A' && ch <= 'Z') {
            adv = 0.65f;
        } else if (ch >= '0' && ch <= '9') {
            adv = 0.58f;
        }
        glyph.advance = adv;

        // Plane bounds in em space: left, bottom, right, top
        glyph.plane_bounds = glm::vec4(0.05f, descender_, adv - 0.05f, ascender_);

        // UV coordinates in atlas [0, 1]: u0, v0, u1, v1
        glyph.uv_bounds = glm::vec4(
            (origin_x + 2.0f) / static_cast<float>(width_),
            (origin_y + 2.0f) / static_cast<float>(height_),
            (origin_x + cell_w - 2.0f) / static_cast<float>(width_),
            (origin_y + cell_h - 2.0f) / static_cast<float>(height_)
        );

        glyphs_[glyph.codepoint] = glyph;

        if (ch == ' ') {
            continue; // Space has no visible glyph geometry
        }

        // Rasterize MSDF glyph pixels inside the cell
        int px_start_x = static_cast<int>(origin_x);
        int px_start_y = static_cast<int>(origin_y);
        int px_end_x = std::min(px_start_x + static_cast<int>(cell_w), static_cast<int>(width_));
        int px_end_y = std::min(px_start_y + static_cast<int>(cell_h), static_cast<int>(height_));

        float cx = origin_x + cell_w * 0.5f;
        float cy = origin_y + cell_h * 0.55f;
        float glyph_w = (adv * em_scale) * 0.85f;
        float glyph_h = em_scale * 0.85f;
        float stroke_thickness = std::max(em_scale * 0.12f, 2.5f);

        for (int y = px_start_y; y < px_end_y; ++y) {
            for (int x = px_start_x; x < px_end_x; ++x) {
                float fx = static_cast<float>(x);
                float fy = static_cast<float>(y);

                // Multi-channel distance fields (r = vertical edges, g = horizontal edges, b = corner/general)
                float d_v = 1e5f;
                float d_h = 1e5f;
                float d_diag = 1e5f;

                float half_w = glyph_w * 0.5f;
                float half_h = glyph_h * 0.5f;

                // Left vertical stroke
                float dl = dist_to_segment(fx, fy, cx - half_w, cy - half_h, cx - half_w, cy + half_h);
                // Right vertical stroke
                float dr = dist_to_segment(fx, fy, cx + half_w, cy - half_h, cx + half_w, cy + half_h);
                // Top horizontal stroke
                float dt = dist_to_segment(fx, fy, cx - half_w, cy - half_h, cx + half_w, cy - half_h);
                // Bottom horizontal stroke
                float db = dist_to_segment(fx, fy, cx - half_w, cy + half_h, cx + half_w, cy + half_h);
                // Middle horizontal bar
                float dm = dist_to_segment(fx, fy, cx - half_w, cy, cx + half_w, cy);

                if (ch >= 'A' && ch <= 'Z') {
                    // Capital letters: sharp vertical and horizontal stems
                    d_v = std::min({dl, dr});
                    d_h = std::min({dt, dm, db});
                    d_diag = dist_to_segment(fx, fy, cx - half_w, cy - half_h, cx + half_w, cy + half_h);
                } else if (ch >= 'a' && ch <= 'z') {
                    // Lowercase letters: slightly shorter height
                    float low_top = cy - half_h * 0.3f;
                    float d_lh = dist_to_segment(fx, fy, cx - half_w, low_top, cx + half_w, low_top);
                    d_v = std::min({dl, dr});
                    d_h = std::min({d_lh, db, dm});
                    d_diag = std::min(dl, dr);
                } else if (ch >= '0' && ch <= '9') {
                    // Numbers
                    d_v = std::min(dl, dr);
                    d_h = std::min({dt, dm, db});
                    d_diag = dist_to_box(fx, fy, cx - half_w, cy - half_h, cx + half_w, cy + half_h);
                } else if (ch == '.') {
                    float d_dot = std::hypot(fx - cx, fy - (cy + half_h * 0.8f)) - stroke_thickness * 0.7f;
                    d_v = d_dot; d_h = d_dot; d_diag = d_dot;
                } else if (ch == ':') {
                    float d_top = std::hypot(fx - cx, fy - (cy - half_h * 0.3f)) - stroke_thickness * 0.6f;
                    float d_bot = std::hypot(fx - cx, fy - (cy + half_h * 0.5f)) - stroke_thickness * 0.6f;
                    float d_col = std::min(d_top, d_bot);
                    d_v = d_col; d_h = d_col; d_diag = d_col;
                } else if (ch == '-' || ch == '_') {
                    float y_level = (ch == '-') ? cy : (cy + half_h * 0.8f);
                    d_h = dist_to_segment(fx, fy, cx - half_w, y_level, cx + half_w, y_level);
                    d_v = d_h; d_diag = d_h;
                } else if (ch == '/' || ch == '\\') {
                    if (ch == '/') d_diag = dist_to_segment(fx, fy, cx - half_w, cy + half_h, cx + half_w, cy - half_h);
                    else d_diag = dist_to_segment(fx, fy, cx - half_w, cy - half_h, cx + half_w, cy + half_h);
                    d_v = d_diag; d_h = d_diag;
                } else {
                    // Punctuation and brackets
                    d_v = std::min(dl, dr);
                    d_h = std::min(dt, db);
                    d_diag = std::min(d_v, d_h);
                }

                // Signed distance: negative inside stroke, positive outside
                float sd_v = d_v - stroke_thickness * 0.5f;
                float sd_h = d_h - stroke_thickness * 0.5f;
                float sd_d = d_diag - stroke_thickness * 0.5f;

                // Map distance to [0, 1] range: 0.5 is edge, >0.5 inside, <0.5 outside
                auto to_msdf_val = [&](float dist) -> uint8_t {
                    float norm = 0.5f - (dist / px_range_);
                    float clamped = std::clamp(norm, 0.0f, 1.0f);
                    return static_cast<uint8_t>(clamped * 255.0f);
                };

                size_t pixel_idx = (y * width_ + x) * 4;
                pixels_[pixel_idx + 0] = to_msdf_val(sd_v); // Red channel (vertical stems)
                pixels_[pixel_idx + 1] = to_msdf_val(sd_h); // Green channel (horizontal bars)
                pixels_[pixel_idx + 2] = to_msdf_val(sd_d); // Blue channel (diagonals / corners)
                pixels_[pixel_idx + 3] = 255;                // Alpha
            }
        }
    }
}

} // namespace cgui::font
