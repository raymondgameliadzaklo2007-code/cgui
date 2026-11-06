#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>

namespace cgui::font {

struct Glyph {
    uint32_t codepoint{0};
    float advance{0.0f};          // Advance width in em units (multiply by font_size)
    glm::vec4 plane_bounds{0.0f}; // left, bottom, right, top relative to baseline in em
    glm::vec4 uv_bounds{0.0f};    // u0, v0, u1, v1 in atlas texture [0, 1]

    [[nodiscard]] float plane_left() const noexcept { return plane_bounds.x; }
    [[nodiscard]] float plane_bottom() const noexcept { return plane_bounds.y; }
    [[nodiscard]] float plane_right() const noexcept { return plane_bounds.z; }
    [[nodiscard]] float plane_top() const noexcept { return plane_bounds.w; }

    [[nodiscard]] float uv_left() const noexcept { return uv_bounds.x; }
    [[nodiscard]] float uv_top() const noexcept { return uv_bounds.y; }
    [[nodiscard]] float uv_right() const noexcept { return uv_bounds.z; }
    [[nodiscard]] float uv_bottom() const noexcept { return uv_bounds.w; }
};

class FontAtlas {
public:
    FontAtlas();
    explicit FontAtlas(const std::string& font_path);
    ~FontAtlas() = default;

    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    [[nodiscard]] const Glyph* get_glyph(uint32_t codepoint) const noexcept;

    // Layout and measurement utilities
    [[nodiscard]] float measure_text_width(std::string_view text, float font_size) const noexcept;
    [[nodiscard]] float measure_text_height(float font_size) const noexcept;

    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] float px_range() const noexcept { return px_range_; }
    [[nodiscard]] float line_height() const noexcept { return line_height_; }
    [[nodiscard]] float ascender() const noexcept { return ascender_; }
    [[nodiscard]] float descender() const noexcept { return descender_; }

    [[nodiscard]] const std::vector<uint8_t>& pixel_data() const noexcept { return pixels_; }

private:
    bool build_with_msdfgen(const std::string& font_path);
    void load_default_atlas();

    uint32_t width_{512};
    uint32_t height_{512};
    float px_range_{4.0f};
    float line_height_{1.25f};
    float ascender_{0.928f};
    float descender_{-0.236f};

    std::unordered_map<uint32_t, Glyph> glyphs_;
    std::vector<uint8_t> pixels_; // RGBA8 MSDF texture data
};

} // namespace cgui::font
