#pragma once

#include "../element.hpp"
#include <string>
#include <functional>
#include <memory>

namespace cgui::ui {

enum class ButtonVariant {
    Primary,
    Secondary,
    Ghost,
    Danger
};

enum class ButtonSize {
    Sm,
    Md,
    Lg
};

class Button {
public:
    explicit Button(std::string label);

    Button& variant(ButtonVariant v);
    Button& primary();
    Button& secondary();
    Button& ghost();
    Button& danger();

    Button& size(ButtonSize s);
    Button& sm();
    Button& md();
    Button& lg();

    Button& disabled(bool is_disabled = true);
    Button& on_click(std::function<void()> callback);

    // Convert into a GPUI Element tree node
    [[nodiscard]] std::shared_ptr<Element> build();
    operator std::shared_ptr<Element>() { return build(); }

private:
    std::string label_;
    ButtonVariant variant_{ButtonVariant::Secondary};
    ButtonSize size_{ButtonSize::Md};
    bool disabled_{false};
    std::function<void()> on_click_;
};

// Fluent factory helper
inline Button button(std::string label) {
    return Button(std::move(label));
}

} // namespace cgui::ui
