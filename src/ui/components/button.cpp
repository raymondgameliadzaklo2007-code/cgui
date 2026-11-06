#include "button.hpp"

namespace cgui::ui {

Button::Button(std::string label) : label_(std::move(label)) {}

Button& Button::variant(ButtonVariant v) {
    variant_ = v;
    return *this;
}

Button& Button::primary() {
    variant_ = ButtonVariant::Primary;
    return *this;
}

Button& Button::secondary() {
    variant_ = ButtonVariant::Secondary;
    return *this;
}

Button& Button::ghost() {
    variant_ = ButtonVariant::Ghost;
    return *this;
}

Button& Button::danger() {
    variant_ = ButtonVariant::Danger;
    return *this;
}

Button& Button::size(ButtonSize s) {
    size_ = s;
    return *this;
}

Button& Button::sm() {
    size_ = ButtonSize::Sm;
    return *this;
}

Button& Button::md() {
    size_ = ButtonSize::Md;
    return *this;
}

Button& Button::lg() {
    size_ = ButtonSize::Lg;
    return *this;
}

Button& Button::disabled(bool is_disabled) {
    disabled_ = is_disabled;
    return *this;
}

Button& Button::on_click(std::function<void()> callback) {
    on_click_ = std::move(callback);
    return *this;
}

std::shared_ptr<Element> Button::build() {
    auto el = row();
    el->align_items(YGAlignCenter).justify_content(YGJustifyCenter);

    // Geometry based on Size
    float h = 34.0f;
    float px = 14.0f;
    float fsize = 14.0f;
    float r = 6.0f;

    switch (size_) {
        case ButtonSize::Sm:
            h = 26.0f;
            px = 10.0f;
            fsize = 12.0f;
            r = 4.0f;
            break;
        case ButtonSize::Md:
            h = 34.0f;
            px = 14.0f;
            fsize = 14.0f;
            r = 6.0f;
            break;
        case ButtonSize::Lg:
            h = 42.0f;
            px = 18.0f;
            fsize = 16.0f;
            r = 8.0f;
            break;
    }

    el->height(h).padding_horizontal(px).radius(r);

    // Styling based on Variant & Disabled state
    if (disabled_) {
        el->bg(Color::hex(0x282C34))
            .border(1.0f, Color::hex(0x3E4451))
            .text(label_, Color::hex(0x5C6370))
            .font_size(fsize);
        return el;
    }

    Color bg_color, hover_color, active_color, border_color, text_color;

    switch (variant_) {
        case ButtonVariant::Primary:
            bg_color     = Color::hex(0x3B82F6); // Blue 500
            hover_color  = Color::hex(0x2563EB); // Blue 600
            active_color = Color::hex(0x1D4ED8); // Blue 700
            border_color = Color::hex(0x60A5FA);
            text_color   = Color::hex(0xFFFFFF);
            break;
        case ButtonVariant::Secondary:
            bg_color     = Color::hex(0x21252B);
            hover_color  = Color::hex(0x2C313C);
            active_color = Color::hex(0x181A1F);
            border_color = Color::hex(0x3E4451);
            text_color   = Color::hex(0xDFE1E5);
            break;
        case ButtonVariant::Ghost:
            bg_color     = Color::rgb(0, 0, 0, 0);
            hover_color  = Color::hex(0x2C313C);
            active_color = Color::hex(0x21252B);
            border_color = Color::rgb(0, 0, 0, 0);
            text_color   = Color::hex(0xABB2BF);
            break;
        case ButtonVariant::Danger:
            bg_color     = Color::hex(0xDC2626); // Red 600
            hover_color  = Color::hex(0xB91C1C); // Red 700
            active_color = Color::hex(0x991B1B); // Red 800
            border_color = Color::hex(0xEF4444);
            text_color   = Color::hex(0xFFFFFF);
            break;
    }

    el->bg(bg_color)
        .hover_bg(hover_color)
        .active_bg(active_color)
        .border(1.0f, border_color)
        .text(label_, text_color)
        .font_size(fsize);

    if (on_click_) {
        el->on_click(on_click_);
    }

    return el;
}

} // namespace cgui::ui
