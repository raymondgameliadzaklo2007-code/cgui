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

    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= (x + width) && py >= y && py <= (y + height);
    }
};

struct Style {
    std::optional<Color> background;
    std::optional<Color> hover_background;
    std::optional<Color> active_background;
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
    Element& width_percent(float percent = 100.0f);
    Element& height_percent(float percent = 100.0f);
    Element& flex_grow(float grow = 1.0f);
    Element& flex_shrink(float shrink = 1.0f);
    Element& flex_basis(float px);
    Element& flex_basis_percent(float percent);
    Element& flex_direction(YGFlexDirection dir);
    Element& justify_content(YGJustify justify);
    Element& align_items(YGAlign align);
    Element& padding(float px);
    Element& padding_horizontal(float px);
    Element& padding_vertical(float px);
    Element& padding_left(float px);
    Element& padding_right(float px);
    Element& padding_top(float px);
    Element& padding_bottom(float px);
    Element& margin(float px);
    Element& margin_horizontal(float px);
    Element& margin_vertical(float px);
    Element& margin_left(float px);
    Element& margin_right(float px);
    Element& margin_top(float px);
    Element& margin_bottom(float px);
    Element& gap(float px);

    // Fluent styling
    Element& bg(const Color& color);
    Element& hover_bg(const Color& color);
    Element& active_bg(const Color& color);
    Element& border(float width, const Color& color);
    Element& radius(float r);
    Element& text(std::string text, const Color& color = Color::rgb(220, 224, 230));
    Element& font_size(float size);
    Element& text_color(const Color& color);

    // Interactive event callbacks
    using ClickCallback = std::function<void()>;
    using HoverCallback = std::function<void(bool is_hovered)>;

    Element& on_click(ClickCallback callback) { on_click_ = std::move(callback); return *this; }
    Element& on_hover(HoverCallback callback) { on_hover_ = std::move(callback); return *this; }

    [[nodiscard]] bool has_click_listener() const noexcept { return static_cast<bool>(on_click_); }
    [[nodiscard]] bool is_hovered() const noexcept { return is_hovered_; }
    void set_hovered(bool hovered) {
        if (is_hovered_ != hovered) {
            is_hovered_ = hovered;
            if (on_hover_) on_hover_(is_hovered_);
        }
    }

    [[nodiscard]] bool is_active() const noexcept { return is_active_; }
    Element& set_active(bool active) { is_active_ = active; return *this; }

    void trigger_click() {
        if (on_click_) on_click_();
    }

    // Hierarchy
    std::shared_ptr<Element> add_child(std::shared_ptr<Element> child);
    void clear_children();

    [[nodiscard]] const std::vector<std::shared_ptr<Element>>& children() const noexcept { return children_; }
    [[nodiscard]] const Style& style() const noexcept { return style_; }
    [[nodiscard]] Bounds computed_bounds() const noexcept;

    // Layout computation entrypoint
    void calculate_layout(float available_width, float available_height);

private:
    YGNodeRef yoga_node_{nullptr};
    Style style_{};
    std::vector<std::shared_ptr<Element>> children_;
    bool is_hovered_{false};
    bool is_active_{false};
    ClickCallback on_click_;
    HoverCallback on_hover_;
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
