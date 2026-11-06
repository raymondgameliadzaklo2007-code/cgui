#include "element.hpp"

namespace cgui::ui {

Element::Element() {
    yoga_node_ = YGNodeNew();
}

Element::~Element() {
    if (yoga_node_) {
        YGNodeFree(yoga_node_);
        yoga_node_ = nullptr;
    }
}

Element& Element::width(float px) {
    YGNodeStyleSetWidth(yoga_node_, px);
    return *this;
}

Element& Element::height(float px) {
    YGNodeStyleSetHeight(yoga_node_, px);
    return *this;
}

Element& Element::width_percent(float percent) {
    YGNodeStyleSetWidthPercent(yoga_node_, percent);
    return *this;
}

Element& Element::height_percent(float percent) {
    YGNodeStyleSetHeightPercent(yoga_node_, percent);
    return *this;
}

Element& Element::flex_grow(float grow) {
    YGNodeStyleSetFlexGrow(yoga_node_, grow);
    return *this;
}

Element& Element::flex_shrink(float shrink) {
    YGNodeStyleSetFlexShrink(yoga_node_, shrink);
    return *this;
}

Element& Element::flex_basis(float px) {
    YGNodeStyleSetFlexBasis(yoga_node_, px);
    return *this;
}

Element& Element::flex_basis_percent(float percent) {
    YGNodeStyleSetFlexBasisPercent(yoga_node_, percent);
    return *this;
}

Element& Element::flex_direction(YGFlexDirection dir) {
    YGNodeStyleSetFlexDirection(yoga_node_, dir);
    return *this;
}

Element& Element::justify_content(YGJustify justify) {
    YGNodeStyleSetJustifyContent(yoga_node_, justify);
    return *this;
}

Element& Element::align_items(YGAlign align) {
    YGNodeStyleSetAlignItems(yoga_node_, align);
    return *this;
}

Element& Element::padding(float px) {
    YGNodeStyleSetPadding(yoga_node_, YGEdgeAll, px);
    return *this;
}

Element& Element::padding_horizontal(float px) {
    YGNodeStyleSetPadding(yoga_node_, YGEdgeHorizontal, px);
    return *this;
}

Element& Element::padding_vertical(float px) {
    YGNodeStyleSetPadding(yoga_node_, YGEdgeVertical, px);
    return *this;
}

Element& Element::padding_left(float px) {
    YGNodeStyleSetPadding(yoga_node_, YGEdgeLeft, px);
    return *this;
}

Element& Element::padding_right(float px) {
    YGNodeStyleSetPadding(yoga_node_, YGEdgeRight, px);
    return *this;
}

Element& Element::padding_top(float px) {
    YGNodeStyleSetPadding(yoga_node_, YGEdgeTop, px);
    return *this;
}

Element& Element::padding_bottom(float px) {
    YGNodeStyleSetPadding(yoga_node_, YGEdgeBottom, px);
    return *this;
}

Element& Element::margin(float px) {
    YGNodeStyleSetMargin(yoga_node_, YGEdgeAll, px);
    return *this;
}

Element& Element::margin_horizontal(float px) {
    YGNodeStyleSetMargin(yoga_node_, YGEdgeHorizontal, px);
    return *this;
}

Element& Element::margin_vertical(float px) {
    YGNodeStyleSetMargin(yoga_node_, YGEdgeVertical, px);
    return *this;
}

Element& Element::margin_left(float px) {
    YGNodeStyleSetMargin(yoga_node_, YGEdgeLeft, px);
    return *this;
}

Element& Element::margin_right(float px) {
    YGNodeStyleSetMargin(yoga_node_, YGEdgeRight, px);
    return *this;
}

Element& Element::margin_top(float px) {
    YGNodeStyleSetMargin(yoga_node_, YGEdgeTop, px);
    return *this;
}

Element& Element::margin_bottom(float px) {
    YGNodeStyleSetMargin(yoga_node_, YGEdgeBottom, px);
    return *this;
}

Element& Element::gap(float px) {
    YGNodeStyleSetGap(yoga_node_, YGGutterAll, px);
    return *this;
}

Element& Element::bg(const Color& color) {
    style_.background = color;
    return *this;
}

Element& Element::hover_bg(const Color& color) {
    style_.hover_background = color;
    return *this;
}

Element& Element::active_bg(const Color& color) {
    style_.active_background = color;
    return *this;
}

Element& Element::border(float width, const Color& color) {
    style_.border_width = width;
    style_.border_color = color;
    YGNodeStyleSetBorder(yoga_node_, YGEdgeAll, width);
    return *this;
}

Element& Element::radius(float r) {
    style_.corner_radius = r;
    return *this;
}

Element& Element::text(std::string text, const Color& color) {
    style_.text = std::move(text);
    style_.text_color = color;
    // Provide natural flexbox bounds if not explicitly overridden
    if (YGNodeStyleGetWidth(yoga_node_).unit == YGUnitUndefined) {
        float estimated_w = static_cast<float>(style_.text.length()) * (style_.font_size * 0.55f);
        YGNodeStyleSetWidth(yoga_node_, estimated_w);
    }
    if (YGNodeStyleGetHeight(yoga_node_).unit == YGUnitUndefined) {
        float estimated_h = style_.font_size * 1.30f;
        YGNodeStyleSetHeight(yoga_node_, estimated_h);
    }
    return *this;
}

Element& Element::font_size(float size) {
    style_.font_size = size;
    return *this;
}

Element& Element::text_color(const Color& color) {
    style_.text_color = color;
    return *this;
}

std::shared_ptr<Element> Element::add_child(std::shared_ptr<Element> child) {
    if (child && child->yoga_node_) {
        YGNodeInsertChild(yoga_node_, child->yoga_node_, YGNodeGetChildCount(yoga_node_));
        children_.push_back(child);
    }
    return child;
}

void Element::clear_children() {
    if (yoga_node_) {
        YGNodeRemoveAllChildren(yoga_node_);
    }
    children_.clear();
}

Bounds Element::computed_bounds() const noexcept {
    return {
        YGNodeLayoutGetLeft(yoga_node_),
        YGNodeLayoutGetTop(yoga_node_),
        YGNodeLayoutGetWidth(yoga_node_),
        YGNodeLayoutGetHeight(yoga_node_)
    };
}

void Element::calculate_layout(float available_width, float available_height) {
    // Crucial: The root node in Yoga has no parent owner. Percentage styles (e.g. 100%)
    // cannot resolve without an owner. Setting explicit width and height on the root
    // ensures the entire flexbox tree resizes dynamically with the window viewport.
    YGNodeStyleSetWidth(yoga_node_, available_width);
    YGNodeStyleSetHeight(yoga_node_, available_height);
    YGNodeCalculateLayout(yoga_node_, available_width, available_height, YGDirectionLTR);
}

} // namespace cgui::ui
