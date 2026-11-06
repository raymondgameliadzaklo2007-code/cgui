#include "event_dispatcher.hpp"
#include <algorithm>

namespace cgui::ui {

EventDispatcher::EventDispatcher(std::shared_ptr<Element> root)
    : root_(std::move(root)) {}

void EventDispatcher::hit_test_all(const std::shared_ptr<Element>& node, float parent_x, float parent_y,
                                   float x, float y, std::vector<HitResult>& out_hits) {
    if (!node) return;

    auto bounds = node->computed_bounds();
    float abs_x = parent_x + bounds.x;
    float abs_y = parent_y + bounds.y;

    Bounds abs_bounds{abs_x, abs_y, bounds.width, bounds.height};

    if (abs_bounds.contains(x, y)) {
        out_hits.push_back({node, abs_x, abs_y});
    }

    // Children are evaluated top-down; deepest child will be visited last
    for (const auto& child : node->children()) {
        hit_test_all(child, abs_x, abs_y, x, y, out_hits);
    }
}

void EventDispatcher::update_hover_states(const std::vector<HitResult>& current_hits) {
    // Collect all elements currently under the pointer
    std::vector<std::shared_ptr<Element>> new_hovered;
    new_hovered.reserve(current_hits.size());
    for (const auto& hit : current_hits) {
        new_hovered.push_back(hit.element);
    }

    // Un-hover elements that are no longer hit
    for (auto it = hovered_elements_.begin(); it != hovered_elements_.end(); ) {
        if (auto el = it->lock()) {
            bool still_hovered = std::find(new_hovered.begin(), new_hovered.end(), el) != new_hovered.end();
            if (!still_hovered) {
                el->set_hovered(false);
                it = hovered_elements_.erase(it);
                continue;
            }
        }
        ++it;
    }

    // Hover newly entered elements
    for (const auto& el : new_hovered) {
        if (!el->is_hovered()) {
            el->set_hovered(true);
            hovered_elements_.push_back(el);
        }
    }
}

void EventDispatcher::on_mouse_move(float x, float y) {
    mouse_x_ = x;
    mouse_y_ = y;

    if (!root_) return;

    std::vector<HitResult> hits;
    hit_test_all(root_, 0.0f, 0.0f, x, y, hits);
    update_hover_states(hits);
}

void EventDispatcher::on_mouse_button(MouseButton button, bool is_down, float x, float y) {
    mouse_x_ = x;
    mouse_y_ = y;

    if (!root_) return;

    std::vector<HitResult> hits;
    hit_test_all(root_, 0.0f, 0.0f, x, y, hits);

    if (button == MouseButton::Left && is_down) {
        // Dispatch click to the deepest hit element with a click listener
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
            it->element->trigger_click();
            break;
        }
    }
}

void EventDispatcher::on_scroll(float /*dx*/, float /*dy*/, float x, float y) {
    mouse_x_ = x;
    mouse_y_ = y;
}

} // namespace cgui::ui
