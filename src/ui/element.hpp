#pragma once

#include <yoga/Yoga.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <glm/glm.hpp>

namespace cgui::ui {

struct Color {
    float r{0.0f}, g{0.0f}, b{0.0f}, a{1.0f};

    static Color rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }
    static Color hex(uint32_t val) {
        return {
            ((val >> 16) & 0xFF) / 255.0f,
            ((val >> 8) & 0xFF) / 255.0f,
            (val & 0xFF) / 255.0f,
            1.0f
        };
    }
};

struct Bounds {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
};

struct Style {
    std::optional<Color> background;
    std::optional<Color> border_color;
    float border_width{0.0f};
    float corner_radius{0.0f};
    std::optional<Color> text_color;
    std::string text;
    float font_size{14.0f};
};

/**
 * Base Element in the GPUI hierarchy.
 * Every Element wraps a Yoga layout node (YGNodeRef).
 */
class Element : public std::enable_shared_from_this<Element> {
public:
    Element();
    virtual ~Element();

    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;

    [[nodiscard]] YGNodeRef yoga_node() const noexcept { return yoga_node_; }

    // Fluent layout chaining
    Element& width(float px);
    Element& height(float px);
    Element& flex_grow(float grow = 1.0f);
    Element& flex_shrink(float shrink = 1.0f);
    Element& flex_direction(YGFlexDirection dir);
    Element& justify_content(YGJustify justify);
    Element& align_items(YGAlign align);
    Element& padding(float px);
    Element& padding_horizontal(float px);
    Element& padding_vertical(float px);
    Element& gap(float px);

    // Fluent styling
    Element& bg(const Color& color);
    Element& border(float width, const Color& color);
    Element& radius(float r);
    Element& text(std::string text, const Color& color = Color::rgb(220, 224, 230));

    // Hierarchy
    std::shared_ptr<Element> add_child(std::shared_ptr<Element> child);

    [[nodiscard]] const std::vector<std::shared_ptr<Element>>& children() const noexcept { return children_; }
    [[nodiscard]] const Style& style() const noexcept { return style_; }
    [[nodiscard]] Bounds computed_bounds() const noexcept;

    // Layout computation entrypoint
    void calculate_layout(float available_width, float available_height);

private:
    YGNodeRef yoga_node_{nullptr};
    Style style_{};
    std::vector<std::shared_ptr<Element>> children_;
};

// Fluent helper factories
inline std::shared_ptr<Element> div() {
    return std::make_shared<Element>();
}

inline std::shared_ptr<Element> row() {
    auto el = std::make_shared<Element>();
    el->flex_direction(YGFlexDirectionRow);
    return el;
}

inline std::shared_ptr<Element> col() {
    auto el = std::make_shared<Element>();
    el->flex_direction(YGFlexDirectionColumn);
    return el;
}

} // namespace cgui::ui
