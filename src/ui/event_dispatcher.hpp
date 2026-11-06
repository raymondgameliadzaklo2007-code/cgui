#pragma once

#include "element.hpp"
#include "event.hpp"
#include <memory>
#include <vector>

namespace cgui::ui {

class EventDispatcher {
public:
    explicit EventDispatcher(std::shared_ptr<Element> root);

    void set_root(std::shared_ptr<Element> root) { root_ = std::move(root); }

    void on_mouse_move(float x, float y);
    void on_mouse_button(MouseButton button, bool is_down, float x, float y);
    void on_scroll(float dx, float dy, float x, float y);

private:
    struct HitResult {
        std::shared_ptr<Element> element;
        float abs_x{0.0f};
        float abs_y{0.0f};
    };

    void hit_test_all(const std::shared_ptr<Element>& node, float parent_x, float parent_y,
                      float x, float y, std::vector<HitResult>& out_hits);

    void update_hover_states(const std::vector<HitResult>& current_hits);

    std::shared_ptr<Element> root_;
    std::vector<std::weak_ptr<Element>> hovered_elements_;
    float mouse_x_{0.0f};
    float mouse_y_{0.0f};
};

} // namespace cgui::ui
