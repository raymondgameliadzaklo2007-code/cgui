#include "font_atlas.hpp"
#include "default_font_data.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <algorithm>

#if __has_include(<msdfgen.h>) && __has_include(<msdfgen-ext.h>)
#include <msdfgen.h>
#include <msdfgen-ext.h>
#define CGUI_HAS_MSDFGEN 1
#else
#define CGUI_HAS_MSDFGEN 0
#endif

namespace cgui::font {

FontAtlas::FontAtlas() {
    const std::vector<std::string> font_candidates = {
        "assets/fonts/font.ttf",
        "../assets/fonts/font.ttf",
        "../../assets/fonts/font.ttf",
        "assets/font.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    };

    bool built = false;
    for (const auto& path : font_candidates) {
        if (std::filesystem::exists(path)) {
            if (build_with_msdfgen(path)) {
                built = true;
                break;
            }
        }
    }

    if (!built) {
        load_default_atlas();
    }
}

FontAtlas::FontAtlas(const std::string& font_path) {
    if (!build_with_msdfgen(font_path)) {
        load_default_atlas();
    }
}

const Glyph* FontAtlas::get_glyph(uint32_t codepoint) const noexcept {
    auto it = glyphs_.find(codepoint);
    if (it != glyphs_.end()) {
        return &it->second;
    }
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

bool FontAtlas::build_with_msdfgen([[maybe_unused]] const std::string& font_path) {
#if CGUI_HAS_MSDFGEN
    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    if (!ft) {
        std::cerr << "[FontAtlas] Failed to initialize FreeType\n";
        return false;
    }

    msdfgen::FontHandle* font = msdfgen::loadFont(ft, font_path.c_str());
    if (!font) {
        msdfgen::deinitializeFreetype(ft);
        return false;
    }

    std::cout << "[FontAtlas] Generating genuine MSDF atlas via msdfgen from: " << font_path << "\n";

    msdfgen::FontMetrics metrics{};
    if (msdfgen::getFontMetrics(metrics, font, msdfgen::FONT_SCALING_EM_NORMALIZED)) {
        ascender_ = static_cast<float>(metrics.ascenderY);
        descender_ = static_cast<float>(metrics.descenderY);
        line_height_ = static_cast<float>(metrics.lineHeight);
    } else {
        ascender_ = 0.928f;
        descender_ = -0.236f;
        line_height_ = 1.25f;
    }

    width_ = 512;
    height_ = 512;
    px_range_ = 4.0f;
    pixels_.assign(width_ * height_ * 4, 0);

    const int cell_size = 48;
    const int cols = 10;
    const double em_scale = 32.0; // 32 pixels per em
    const double origin_x = 8.0;
    const double origin_y = 10.0;

    glyphs_.clear();

    for (uint32_t cp = 32; cp <= 126; ++cp) {
        int index = static_cast<int>(cp - 32);
        int col = index % cols;
        int row = index / cols;
        int cell_x = col * cell_size;
        int cell_y = row * cell_size;

        double advance = 0.0;
        msdfgen::Shape shape;
        if (!msdfgen::loadGlyph(shape, font, cp, msdfgen::FONT_SCALING_EM_NORMALIZED, &advance)) {
            continue;
        }

        Glyph glyph{};
        glyph.codepoint = cp;
        glyph.advance = static_cast<float>(advance);

        // Uniform em cell boundaries
        float pl = static_cast<float>(-origin_x / em_scale);
        float pb = static_cast<float>(-origin_y / em_scale);
        float pr = pl + static_cast<float>(cell_size / em_scale);
        float pt = pb + static_cast<float>(cell_size / em_scale);
        glyph.plane_bounds = glm::vec4(pl, pb, pr, pt);

        float u0 = static_cast<float>(cell_x) / static_cast<float>(width_);
        float v0 = static_cast<float>(cell_y) / static_cast<float>(height_);
        float u1 = static_cast<float>(cell_x + cell_size) / static_cast<float>(width_);
        float v1 = static_cast<float>(cell_y + cell_size) / static_cast<float>(height_);
        glyph.uv_bounds = glm::vec4(u0, v0, u1, v1);

        glyphs_[cp] = glyph;

        if (cp == 32 || shape.contours.empty()) {
            continue;
        }

        shape.normalize();
        msdfgen::edgeColoringSimple(shape, 3.0);

        msdfgen::Bitmap<float, 3> msdf(cell_size, cell_size);
        msdfgen::generateMSDF(
            msdf,
            shape,
            msdfgen::Range(px_range_ / em_scale),
            msdfgen::Vector2(em_scale, em_scale),
            msdfgen::Vector2(origin_x / em_scale, origin_y / em_scale)
        );

        // Copy with flipped Y (msdfgen y=0 is bottom, Vulkan y=0 is top)
        for (int y = 0; y < cell_size; ++y) {
            int atlas_y = cell_y + (cell_size - 1 - y);
            for (int x = 0; x < cell_size; ++x) {
                int atlas_x = cell_x + x;
                float r = msdf(x, y)[0];
                float g = msdf(x, y)[1];
                float b = msdf(x, y)[2];

                size_t p_idx = (static_cast<size_t>(atlas_y) * width_ + atlas_x) * 4;
                pixels_[p_idx + 0] = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
                pixels_[p_idx + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
                pixels_[p_idx + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
                pixels_[p_idx + 3] = 255;
            }
        }
    }

    msdfgen::destroyFont(font);
    msdfgen::deinitializeFreetype(ft);

    std::cout << "[FontAtlas] Successfully generated " << glyphs_.size()
              << " MSDF glyphs (" << width_ << "x" << height_ << ")\n";
    return true;
#else
    return false;
#endif
}

void FontAtlas::load_default_atlas() {
    width_ = DEFAULT_ATLAS_WIDTH;
    height_ = DEFAULT_ATLAS_HEIGHT;
    px_range_ = 4.0f;
    line_height_ = 1.25f;
    ascender_ = 0.928f;
    descender_ = -0.236f;

    // 1. Populate TrueType glyph metrics from the precomputed embedded table
    glyphs_.clear();
    for (size_t i = 0; i < EMBEDDED_GLYPH_COUNT; ++i) {
        const auto& raw = EMBEDDED_GLYPHS[i];
        Glyph g{};
        g.codepoint = raw.codepoint;
        g.advance = raw.advance;
        g.plane_bounds = glm::vec4(raw.plane_left, raw.plane_bottom, raw.plane_right, raw.plane_top);
        g.uv_bounds = glm::vec4(raw.uv_left, raw.uv_top, raw.uv_right, raw.uv_bottom);
        glyphs_[g.codepoint] = g;
    }

    // 2. Load the binary MSDF atlas texture
    const std::vector<std::string> search_paths = {
        "assets/fonts/font_atlas.bin",
        "../assets/fonts/font_atlas.bin",
        "../../assets/fonts/font_atlas.bin"
    };

    bool loaded = false;
    for (const auto& path : search_paths) {
        if (std::filesystem::exists(path)) {
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                uint32_t w = 0, h = 0;
                file.read(reinterpret_cast<char*>(&w), sizeof(w));
                file.read(reinterpret_cast<char*>(&h), sizeof(h));
                if (w > 0 && h > 0) {
                    width_ = w;
                    height_ = h;
                    size_t data_size = static_cast<size_t>(w) * h * 4;
                    pixels_.resize(data_size);
                    file.read(reinterpret_cast<char*>(pixels_.data()), data_size);
                    loaded = true;
                    std::cout << "[FontAtlas] Successfully loaded genuine MSDF atlas from " << path
                              << " (" << w << "x" << h << ")\n";
                    break;
                }
            }
        }
    }

    if (!loaded) {
        std::cerr << "[FontAtlas Warning] Could not load assets/fonts/font_atlas.bin. Initializing fallback buffer.\n";
        pixels_.assign(width_ * height_ * 4, 0);
    }
}

} // namespace cgui::font
