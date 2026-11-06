#include "font_atlas.hpp"
#include "default_font_data.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace cgui::font {

FontAtlas::FontAtlas() {
    generate_default_atlas();
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

void FontAtlas::generate_default_atlas() {
    width_ = DEFAULT_ATLAS_WIDTH;
    height_ = DEFAULT_ATLAS_HEIGHT;
    px_range_ = 4.0f;
    line_height_ = 1.25f;
    ascender_ = 0.85f;
    descender_ = -0.25f;

    // 1. Populate exact TrueType glyph metrics from the precomputed embedded table
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

    // 2. Load the binary MSDF/SDF atlas texture
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
                    std::cout << "[FontAtlas] Successfully loaded FreeType distance-field atlas from " << path
                              << " (" << w << "x" << h << ")\n";
                    break;
                }
            }
        }
    }

    if (!loaded) {
        std::cerr << "[FontAtlas Warning] Could not find assets/fonts/font_atlas.bin. Generating fallback buffer.\n";
        pixels_.assign(width_ * height_ * 4, 128);
    }
}

} // namespace cgui::font
