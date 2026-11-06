#include "core/window.hpp"
#include "renderer/vulkan_context.hpp"
#include "renderer/vulkan_2d.hpp"
#include "ui/element.hpp"
#include "ui/event_dispatcher.hpp"
#include "ui/event.hpp"

#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>

namespace {

// One Dark / Zed inspired color theme
namespace Theme {
    const auto BgApp         = cgui::ui::Color::hex(0x181A1F);
    const auto BgTitlebar    = cgui::ui::Color::hex(0x21252B);
    const auto BgActivityBar = cgui::ui::Color::hex(0x1E2227);
    const auto BgSidebar     = cgui::ui::Color::hex(0x21252B);
    const auto BgEditor      = cgui::ui::Color::hex(0x282C34);
    const auto BgTabActive   = cgui::ui::Color::hex(0x282C34);
    const auto BgTabInactive = cgui::ui::Color::hex(0x21252B);
    const auto BgStatusBar   = cgui::ui::Color::hex(0x1E2227);
    const auto Border        = cgui::ui::Color::hex(0x181A1F);
    const auto HoverLight    = cgui::ui::Color::hex(0x2C313C);
    const auto HoverMuted    = cgui::ui::Color::hex(0x353B45);

    // Syntax & Accent colors
    const auto AccentBlue    = cgui::ui::Color::hex(0x61AFEF);
    const auto AccentGreen   = cgui::ui::Color::hex(0x98C379);
    const auto AccentPurple  = cgui::ui::Color::hex(0xC678DD);
    const auto AccentYellow  = cgui::ui::Color::hex(0xE5C07B);
    const auto AccentRed     = cgui::ui::Color::hex(0xE06C75);
    const auto AccentOrange  = cgui::ui::Color::hex(0xD19A66);
    const auto AccentTeal    = cgui::ui::Color::hex(0x56B6C2);

    const auto TextNormal    = cgui::ui::Color::hex(0xABB2BF);
    const auto TextMuted     = cgui::ui::Color::hex(0x5C6370);
    const auto TextBright    = cgui::ui::Color::hex(0xE6EDF3);
}

struct EditorState {
    std::string active_file = "src/main.cpp";
    std::vector<std::string> open_tabs = {
        "main.cpp",
        "window.hpp",
        "element.hpp",
        "vulkan_2d.cpp"
    };
    int active_tab_index = 0;
    bool sidebar_collapsed = false;
    std::string active_activity = "explorer"; // explorer, search, git, debug, settings
    int cursor_line = 14;
    int cursor_col = 22;
    std::string branch = "main*";
};

using ActionCallback = std::function<void()>;

// Helper to create small rounded indicator badges / icons
std::shared_ptr<cgui::ui::Element> create_badge(float w, float h, const cgui::ui::Color& color, float radius = 3.0f) {
    auto el = cgui::ui::div();
    el->width(w).height(h).bg(color).radius(radius);
    return el;
}

// ---------------------------------------------------------------------------
// 1. Header / Title Bar
// ---------------------------------------------------------------------------
std::shared_ptr<cgui::ui::Element> build_titlebar(EditorState& state, ActionCallback mark_dirty) {
    auto titlebar = cgui::ui::row();
    titlebar->height(38.0f)
        .padding_horizontal(14.0f)
        .align_items(YGAlignCenter)
        .justify_content(YGJustifySpaceBetween)
        .bg(Theme::BgTitlebar)
        .border(1.0f, Theme::Border);

    // Left: Window control dots + App Branding
    auto left_group = cgui::ui::row();
    left_group->align_items(YGAlignCenter).gap(8.0f);

    auto dot_close = cgui::ui::div();
    dot_close->width(12.0f).height(12.0f).radius(6.0f).bg(Theme::AccentRed).hover_bg(cgui::ui::Color::hex(0xFF6B68));
    auto dot_min = cgui::ui::div();
    dot_min->width(12.0f).height(12.0f).radius(6.0f).bg(Theme::AccentYellow).hover_bg(cgui::ui::Color::hex(0xFFD368));
    auto dot_max = cgui::ui::div();
    dot_max->width(12.0f).height(12.0f).radius(6.0f).bg(Theme::AccentGreen).hover_bg(cgui::ui::Color::hex(0x82DC70));

    left_group->add_child(dot_close);
    left_group->add_child(dot_min);
    left_group->add_child(dot_max);

    auto logo_spacer = cgui::ui::div();
    logo_spacer->width(8.0f);
    left_group->add_child(logo_spacer);

    auto app_title = cgui::ui::div();
    app_title->text("cgui GPUI Studio", Theme::TextBright);
    left_group->add_child(app_title);

    titlebar->add_child(left_group);

    // Center: Active File Path Pill
    auto center_pill = cgui::ui::row();
    center_pill->height(24.0f)
        .padding_horizontal(16.0f)
        .align_items(YGAlignCenter)
        .radius(6.0f)
        .bg(Theme::BgApp)
        .border(1.0f, Theme::Border)
        .gap(6.0f);

    auto pill_file = cgui::ui::div();
    pill_file->text(state.active_file, Theme::AccentBlue);
    center_pill->add_child(pill_file);
    titlebar->add_child(center_pill);

    // Right: Quick Actions (Run/Build button, Sidebar toggle)
    auto right_group = cgui::ui::row();
    right_group->align_items(YGAlignCenter).gap(8.0f);

    auto run_btn = cgui::ui::row();
    run_btn->height(26.0f)
        .padding_horizontal(12.0f)
        .align_items(YGAlignCenter)
        .radius(4.0f)
        .bg(Theme::HoverLight)
        .hover_bg(Theme::HoverMuted)
        .on_click([mark_dirty]() {
            std::cout << "[Action] Re-running build pipeline...\n";
            mark_dirty();
        });

    auto run_indicator = create_badge(8.0f, 8.0f, Theme::AccentGreen, 4.0f);
    run_btn->add_child(run_indicator);

    auto run_text = cgui::ui::div();
    run_text->margin_left(6.0f).text("Build & Run", Theme::TextNormal);
    run_btn->add_child(run_text);
    right_group->add_child(run_btn);

    auto toggle_sidebar_btn = cgui::ui::div();
    toggle_sidebar_btn->width(28.0f).height(26.0f).radius(4.0f)
        .bg(state.sidebar_collapsed ? Theme::BgApp : Theme::HoverLight)
        .hover_bg(Theme::HoverMuted)
        .on_click([&state, mark_dirty]() {
            state.sidebar_collapsed = !state.sidebar_collapsed;
            mark_dirty();
        });
    right_group->add_child(toggle_sidebar_btn);

    titlebar->add_child(right_group);
    return titlebar;
}

// ---------------------------------------------------------------------------
// 2. Activity Bar (Far Left Rail)
// ---------------------------------------------------------------------------
std::shared_ptr<cgui::ui::Element> build_activity_bar(EditorState& state, ActionCallback mark_dirty) {
    auto activity_bar = cgui::ui::col();
    activity_bar->width(48.0f)
        .height_percent(100.0f)
        .bg(Theme::BgActivityBar)
        .border(1.0f, Theme::Border)
        .padding_vertical(8.0f)
        .align_items(YGAlignCenter)
        .justify_content(YGJustifySpaceBetween);

    auto top_icons = cgui::ui::col();
    top_icons->gap(8.0f).align_items(YGAlignCenter);

    auto add_activity_btn = [&](const std::string& id, const cgui::ui::Color& tint) {
        bool is_active = (state.active_activity == id) && !state.sidebar_collapsed;

        auto item = cgui::ui::row();
        item->width(40.0f).height(40.0f).radius(6.0f)
            .align_items(YGAlignCenter).justify_content(YGJustifyCenter)
            .bg(is_active ? Theme::HoverLight : Theme::BgActivityBar)
            .hover_bg(Theme::HoverLight)
            .on_click([&state, id, mark_dirty]() {
                if (state.active_activity == id) {
                    state.sidebar_collapsed = !state.sidebar_collapsed;
                } else {
                    state.active_activity = id;
                    state.sidebar_collapsed = false;
                }
                mark_dirty();
            });

        // Small colored block simulating the tool icon
        auto icon_glyph = create_badge(18.0f, 18.0f, is_active ? tint : Theme::TextMuted, 4.0f);
        item->add_child(icon_glyph);
        top_icons->add_child(item);
    };

    add_activity_btn("explorer", Theme::AccentBlue);
    add_activity_btn("search", Theme::AccentPurple);
    add_activity_btn("git", Theme::AccentOrange);
    add_activity_btn("debug", Theme::AccentGreen);

    activity_bar->add_child(top_icons);

    // Bottom settings icon
    auto bottom_icons = cgui::ui::col();
    bottom_icons->gap(8.0f).align_items(YGAlignCenter);

    auto settings_btn = cgui::ui::div();
    settings_btn->width(40.0f).height(40.0f).radius(6.0f)
        .bg(Theme::BgActivityBar).hover_bg(Theme::HoverLight);
    auto settings_icon = create_badge(16.0f, 16.0f, Theme::TextMuted, 8.0f);
    settings_btn->add_child(settings_icon);

    bottom_icons->add_child(settings_btn);
    activity_bar->add_child(bottom_icons);

    return activity_bar;
}

// ---------------------------------------------------------------------------
// 3. Collapsible Sidebar (Project File Tree)
// ---------------------------------------------------------------------------
std::shared_ptr<cgui::ui::Element> build_sidebar(EditorState& state, ActionCallback mark_dirty) {
    auto sidebar = cgui::ui::col();
    sidebar->width(250.0f)
        .height_percent(100.0f)
        .bg(Theme::BgSidebar)
        .border(1.0f, Theme::Border)
        .padding(10.0f);

    // Section Header: "EXPLORER"
    auto header = cgui::ui::row();
    header->height(26.0f).align_items(YGAlignCenter).justify_content(YGJustifySpaceBetween);

    auto title = cgui::ui::div();
    title->text("CGUI PROJECT", Theme::TextMuted);
    header->add_child(title);

    sidebar->add_child(header);

    // File Tree Items
    struct TreeFile {
        std::string name;
        std::string full_path;
        int depth;
        bool is_dir;
        cgui::ui::Color icon_color;
    };

    const std::vector<TreeFile> tree_files = {
        {"src", "src", 0, true, Theme::AccentBlue},
        {"core", "src/core", 1, true, Theme::AccentYellow},
        {"window.hpp", "src/core/window.hpp", 2, false, Theme::AccentPurple},
        {"window.cpp", "src/core/window.cpp", 2, false, Theme::AccentBlue},
        {"renderer", "src/renderer", 1, true, Theme::AccentYellow},
        {"vulkan_context.hpp", "src/renderer/vulkan_context.hpp", 2, false, Theme::AccentPurple},
        {"vulkan_2d.hpp", "src/renderer/vulkan_2d.hpp", 2, false, Theme::AccentPurple},
        {"quad_pipeline.cpp", "src/renderer/quad_pipeline.cpp", 2, false, Theme::AccentBlue},
        {"ui", "src/ui", 1, true, Theme::AccentYellow},
        {"element.hpp", "src/ui/element.hpp", 2, false, Theme::AccentPurple},
        {"event_dispatcher.hpp", "src/ui/event_dispatcher.hpp", 2, false, Theme::AccentPurple},
        {"main.cpp", "src/main.cpp", 1, false, Theme::AccentBlue},
        {"CMakeLists.txt", "CMakeLists.txt", 0, false, Theme::AccentRed}
    };

    auto list_container = cgui::ui::col();
    list_container->gap(2.0f).margin_top(6.0f);

    for (const auto& item : tree_files) {
        bool is_selected = (state.active_file == item.full_path);

        auto row = cgui::ui::row();
        row->height(24.0f)
            .radius(4.0f)
            .padding_horizontal(6.0f + (item.depth * 14.0f))
            .align_items(YGAlignCenter)
            .bg(is_selected ? Theme::HoverLight : cgui::ui::Color::rgb(0, 0, 0, 0))
            .hover_bg(Theme::HoverLight)
            .on_click([&state, full_path = item.full_path, file_name = item.name, mark_dirty, is_dir = item.is_dir]() {
                if (is_dir) return;
                state.active_file = full_path;

                // Add to tabs if not present
                auto it = std::find(state.open_tabs.begin(), state.open_tabs.end(), file_name);
                if (it == state.open_tabs.end()) {
                    state.open_tabs.push_back(file_name);
                    state.active_tab_index = static_cast<int>(state.open_tabs.size() - 1);
                } else {
                    state.active_tab_index = static_cast<int>(std::distance(state.open_tabs.begin(), it));
                }
                mark_dirty();
            });

        // Small indicator icon
        auto icon = create_badge(item.is_dir ? 10.0f : 8.0f, item.is_dir ? 8.0f : 8.0f, item.icon_color, 2.0f);
        row->add_child(icon);

        auto label = cgui::ui::div();
        label->margin_left(8.0f).text(item.name, is_selected ? Theme::TextBright : Theme::TextNormal);
        row->add_child(label);

        list_container->add_child(row);
    }

    sidebar->add_child(list_container);
    return sidebar;
}

// ---------------------------------------------------------------------------
// 4. Editor Area (Tab Bar + Breadcrumbs + Code Surface + Minimap)
// ---------------------------------------------------------------------------
std::shared_ptr<cgui::ui::Element> build_editor_area(EditorState& state, ActionCallback mark_dirty) {
    auto editor = cgui::ui::col();
    editor->flex_grow(1.0f)
        .height_percent(100.0f)
        .bg(Theme::BgEditor);

    // Tab Bar (Height: 36px)
    auto tab_bar = cgui::ui::row();
    tab_bar->height(36.0f)
        .bg(Theme::BgTitlebar)
        .border(1.0f, Theme::Border)
        .align_items(YGAlignFlexEnd);

    for (size_t i = 0; i < state.open_tabs.size(); ++i) {
        bool is_active = (static_cast<int>(i) == state.active_tab_index);
        const auto& tab_name = state.open_tabs[i];

        auto tab = cgui::ui::row();
        tab->height(32.0f)
            .padding_horizontal(14.0f)
            .align_items(YGAlignCenter)
            .bg(is_active ? Theme::BgTabActive : Theme::BgTabInactive)
            .hover_bg(is_active ? Theme::BgTabActive : Theme::HoverLight)
            .border(is_active ? 2.0f : 1.0f, is_active ? Theme::AccentBlue : Theme::Border)
            .radius(4.0f)
            .gap(8.0f)
            .on_click([&state, i, mark_dirty]() {
                state.active_tab_index = static_cast<int>(i);
                mark_dirty();
            });

        auto dot = create_badge(6.0f, 6.0f, is_active ? Theme::AccentBlue : Theme::TextMuted, 3.0f);
        tab->add_child(dot);

        auto tab_label = cgui::ui::div();
        tab_label->text(tab_name, is_active ? Theme::TextBright : Theme::TextNormal);
        tab->add_child(tab_label);

        // Close 'x' button
        auto close_btn = cgui::ui::div();
        close_btn->width(12.0f).height(12.0f).radius(6.0f)
            .bg(cgui::ui::Color::rgb(0, 0, 0, 0))
            .hover_bg(Theme::HoverMuted)
            .on_click([&state, i, mark_dirty]() {
                if (state.open_tabs.size() > 1) {
                    state.open_tabs.erase(state.open_tabs.begin() + i);
                    if (state.active_tab_index >= static_cast<int>(state.open_tabs.size())) {
                        state.active_tab_index = static_cast<int>(state.open_tabs.size() - 1);
                    }
                    mark_dirty();
                }
            });
        tab->add_child(close_btn);

        tab_bar->add_child(tab);
    }

    editor->add_child(tab_bar);

    // Breadcrumb strip (Height: 26px)
    auto breadcrumbs = cgui::ui::row();
    breadcrumbs->height(26.0f)
        .padding_horizontal(16.0f)
        .align_items(YGAlignCenter)
        .bg(Theme::BgEditor)
        .border(1.0f, Theme::Border)
        .gap(8.0f);

    auto bc_label = cgui::ui::div();
    bc_label->text("cgui  >  " + state.active_file, Theme::TextMuted);
    breadcrumbs->add_child(bc_label);
    editor->add_child(breadcrumbs);

    // Code Surface: Gutter + Code Lines + Minimap
    auto code_surface = cgui::ui::row();
    code_surface->flex_grow(1.0f);

    // Gutter with Line Numbers
    auto gutter = cgui::ui::col();
    gutter->width(50.0f)
        .height_percent(100.0f)
        .bg(Theme::BgEditor)
        .border(1.0f, Theme::Border)
        .padding_vertical(8.0f)
        .align_items(YGAlignCenter)
        .gap(6.0f);

    for (int ln = 1; ln <= 28; ++ln) {
        auto num_item = cgui::ui::div();
        num_item->height(18.0f);
        if (ln == state.cursor_line) {
            num_item->text(std::to_string(ln), Theme::AccentBlue);
        } else {
            num_item->text(std::to_string(ln), Theme::TextMuted);
        }
        gutter->add_child(num_item);
    }
    code_surface->add_child(gutter);

    // Code Lines Buffer (declaratively structured with syntax-highlighted blocks)
    auto code_content = cgui::ui::col();
    code_content->flex_grow(1.0f)
        .height_percent(100.0f)
        .padding_vertical(8.0f)
        .padding_horizontal(16.0f)
        .gap(6.0f);

    // Simulate rich code syntax tokens across lines
    auto make_code_line = [&](int line_num, const std::vector<std::pair<float, cgui::ui::Color>>& tokens, float indent = 0.0f) {
        auto line_row = cgui::ui::row();
        line_row->height(18.0f).align_items(YGAlignCenter).gap(6.0f);

        if (line_num == state.cursor_line) {
            line_row->bg(Theme::HoverLight).radius(3.0f).padding_horizontal(4.0f);
        }

        if (indent > 0.0f) {
            auto indent_box = cgui::ui::div();
            indent_box->width(indent);
            line_row->add_child(indent_box);
        }

        for (const auto& [w, color] : tokens) {
            auto token = create_badge(w, 10.0f, color, 2.0f);
            line_row->add_child(token);
        }

        // Caret on the active line
        if (line_num == state.cursor_line) {
            auto caret = cgui::ui::div();
            caret->width(2.0f).height(14.0f).bg(Theme::AccentBlue).radius(1.0f);
            line_row->add_child(caret);
        }

        return line_row;
    };

    // Lines representing realistic C++23 source code
    code_content->add_child(make_code_line(1, {{65.0f, Theme::AccentPurple}, {140.0f, Theme::AccentGreen}}));
    code_content->add_child(make_code_line(2, {{65.0f, Theme::AccentPurple}, {180.0f, Theme::AccentGreen}}));
    code_content->add_child(make_code_line(3, {{65.0f, Theme::AccentPurple}, {160.0f, Theme::AccentGreen}}));
    code_content->add_child(make_code_line(4, {{220.0f, Theme::TextMuted}})); // comment
    code_content->add_child(make_code_line(5, {{35.0f, Theme::AccentPurple}, {40.0f, Theme::AccentBlue}, {20.0f, Theme::TextNormal}}));
    code_content->add_child(make_code_line(6, {{40.0f, Theme::AccentPurple}, {10.0f, Theme::TextNormal}}, 16.0f));
    code_content->add_child(make_code_line(7, {{120.0f, Theme::AccentYellow}, {60.0f, Theme::AccentBlue}, {140.0f, Theme::AccentGreen}}, 32.0f));
    code_content->add_child(make_code_line(8, {{90.0f, Theme::AccentYellow}, {80.0f, Theme::AccentBlue}}, 32.0f));
    code_content->add_child(make_code_line(9, {{130.0f, Theme::AccentYellow}, {70.0f, Theme::AccentBlue}}, 32.0f));
    code_content->add_child(make_code_line(10, {{180.0f, Theme::TextMuted}}, 32.0f));
    code_content->add_child(make_code_line(11, {{50.0f, Theme::AccentPurple}, {120.0f, Theme::AccentTeal}, {30.0f, Theme::TextNormal}}, 32.0f));
    code_content->add_child(make_code_line(12, {{110.0f, Theme::AccentBlue}, {45.0f, Theme::AccentOrange}}, 32.0f));
    code_content->add_child(make_code_line(13, {{140.0f, Theme::AccentBlue}, {60.0f, Theme::AccentGreen}}, 32.0f));
    code_content->add_child(make_code_line(14, {{160.0f, Theme::AccentBlue}, {90.0f, Theme::AccentYellow}}, 32.0f)); // active
    code_content->add_child(make_code_line(15, {{45.0f, Theme::AccentPurple}, {30.0f, Theme::AccentOrange}}, 32.0f));
    code_content->add_child(make_code_line(16, {{10.0f, Theme::TextNormal}}, 16.0f));
    code_content->add_child(make_code_line(17, {{10.0f, Theme::TextNormal}}));

    code_surface->add_child(code_content);

    // Minimap Strip on the Right Edge (Width: 44px)
    auto minimap = cgui::ui::col();
    minimap->width(44.0f)
        .height_percent(100.0f)
        .bg(Theme::BgTitlebar)
        .border(1.0f, Theme::Border)
        .padding_vertical(8.0f)
        .padding_horizontal(4.0f)
        .gap(3.0f);

    for (int i = 0; i < 28; ++i) {
        float mw = (i % 3 == 0) ? 28.0f : ((i % 2 == 0) ? 36.0f : 20.0f);
        auto bar = create_badge(mw, 3.0f, (i == state.cursor_line) ? Theme::AccentBlue : Theme::HoverMuted, 1.0f);
        minimap->add_child(bar);
    }

    code_surface->add_child(minimap);
    editor->add_child(code_surface);

    return editor;
}

// ---------------------------------------------------------------------------
// 5. Status Bar (Bottom Strip)
// ---------------------------------------------------------------------------
std::shared_ptr<cgui::ui::Element> build_status_bar(EditorState& state) {
    auto status_bar = cgui::ui::row();
    status_bar->height(26.0f)
        .padding_horizontal(12.0f)
        .align_items(YGAlignCenter)
        .justify_content(YGJustifySpaceBetween)
        .bg(Theme::BgStatusBar)
        .border(1.0f, Theme::Border);

    // Left Status Items (Branch, Errors, Status)
    auto left_items = cgui::ui::row();
    left_items->align_items(YGAlignCenter).gap(12.0f);

    auto branch_pill = cgui::ui::row();
    branch_pill->align_items(YGAlignCenter).gap(4.0f);
    auto branch_dot = create_badge(6.0f, 6.0f, Theme::AccentGreen, 3.0f);
    branch_pill->add_child(branch_dot);
    auto branch_name = cgui::ui::div();
    branch_name->text(state.branch, Theme::TextBright);
    branch_pill->add_child(branch_name);
    left_items->add_child(branch_pill);

    auto errors_pill = cgui::ui::row();
    errors_pill->align_items(YGAlignCenter).gap(4.0f);
    auto err_dot = create_badge(6.0f, 6.0f, Theme::AccentRed, 3.0f);
    errors_pill->add_child(err_dot);
    auto err_text = cgui::ui::div();
    err_text->text("0", Theme::TextNormal);
    errors_pill->add_child(err_text);
    left_items->add_child(errors_pill);

    auto ready_label = cgui::ui::div();
    ready_label->text("Ready (Vulkan 2D Engine)", Theme::TextMuted);
    left_items->add_child(ready_label);

    status_bar->add_child(left_items);

    // Right Status Items (Ln/Col, Encoding, Language)
    auto right_items = cgui::ui::row();
    right_items->align_items(YGAlignCenter).gap(14.0f);

    auto pos_label = cgui::ui::div();
    pos_label->text("Ln " + std::to_string(state.cursor_line) + ", Col " + std::to_string(state.cursor_col), Theme::TextNormal);
    right_items->add_child(pos_label);

    auto spaces_label = cgui::ui::div();
    spaces_label->text("Spaces: 4", Theme::TextNormal);
    right_items->add_child(spaces_label);

    auto enc_label = cgui::ui::div();
    enc_label->text("UTF-8", Theme::TextNormal);
    right_items->add_child(enc_label);

    auto lang_badge = cgui::ui::div();
    lang_badge->text("C++23", Theme::AccentBlue);
    right_items->add_child(lang_badge);

    status_bar->add_child(right_items);

    return status_bar;
}

// ---------------------------------------------------------------------------
// Main UI Tree Assembler
// ---------------------------------------------------------------------------
std::shared_ptr<cgui::ui::Element> build_entire_tree(EditorState& state, ActionCallback mark_dirty) {
    auto root = cgui::ui::col();
    root->bg(Theme::BgApp);

    // 1. Top Header / Titlebar
    root->add_child(build_titlebar(state, mark_dirty));

    // 2. Middle Work Area (Activity Bar + Sidebar + Main Editor)
    auto body = cgui::ui::row();
    body->flex_grow(1.0f);

    // Far Left Activity Rail
    body->add_child(build_activity_bar(state, mark_dirty));

    // Collapsible Project Explorer
    if (!state.sidebar_collapsed) {
        body->add_child(build_sidebar(state, mark_dirty));
    }

    // Code Editor Workspace
    body->add_child(build_editor_area(state, mark_dirty));

    root->add_child(body);

    // 3. Bottom Status Bar
    root->add_child(build_status_bar(state));

    return root;
}

} // anonymous namespace

int main() {
    try {
        std::cout << "Starting cgui GPUI Studio (C++23 + Vulkan + GLFW + Yoga)...\n";

        // 1. Initialize Platform Window
        cgui::core::Window window({
            .title = "cgui – GPUI Studio",
            .width = 1280,
            .height = 800,
            .resizable = true
        });

        // 2. Initialize Vulkan Backend
        cgui::renderer::VulkanContext vk_context(window.handle());
        cgui::renderer::Vulkan2DRenderer renderer(vk_context);

        // 3. App State & Dynamic Layout Tree
        EditorState state{};
        bool needs_rebuild = true;
        auto mark_dirty = [&]() {
            needs_rebuild = true;
        };

        auto root = build_entire_tree(state, mark_dirty);

        // 4. Interactive Event Dispatcher
        cgui::ui::EventDispatcher event_dispatcher(root);

        // Hook up window resizing
        window.on_resize([&](int /*w*/, int /*h*/) {
            vk_context.recreate_swapchain();
            renderer.on_swapchain_resized();
        });

        // High-DPI aware cursor movement
        window.on_mouse_move([&](double xpos, double ypos) {
            auto [fb_w, fb_h] = window.framebuffer_size();
            auto [win_w, win_h] = window.window_size();
            float scale_x = (win_w > 0) ? (static_cast<float>(fb_w) / static_cast<float>(win_w)) : 1.0f;
            float scale_y = (win_h > 0) ? (static_cast<float>(fb_h) / static_cast<float>(win_h)) : 1.0f;

            event_dispatcher.on_mouse_move(static_cast<float>(xpos) * scale_x,
                                           static_cast<float>(ypos) * scale_y);
        });

        // Mouse click handling
        window.on_mouse_button([&](int button, int action, int /*mods*/) {
            auto [fb_w, fb_h] = window.framebuffer_size();
            auto [win_w, win_h] = window.window_size();
            float scale_x = (win_w > 0) ? (static_cast<float>(fb_w) / static_cast<float>(win_w)) : 1.0f;
            float scale_y = (win_h > 0) ? (static_cast<float>(fb_h) / static_cast<float>(win_h)) : 1.0f;

            double xpos = 0.0, ypos = 0.0;
            glfwGetCursorPos(window.handle(), &xpos, &ypos);

            cgui::ui::MouseButton mb = cgui::ui::MouseButton::Left;
            if (button == GLFW_MOUSE_BUTTON_RIGHT) mb = cgui::ui::MouseButton::Right;
            else if (button == GLFW_MOUSE_BUTTON_MIDDLE) mb = cgui::ui::MouseButton::Middle;

            event_dispatcher.on_mouse_button(mb, action == GLFW_PRESS,
                                            static_cast<float>(xpos) * scale_x,
                                            static_cast<float>(ypos) * scale_y);
        });

        std::cout << "Engine & UI initialized. Entering main event loop.\n";

        // Main Loop
        while (!window.should_close()) {
            window.poll_events();

            if (needs_rebuild) {
                root = build_entire_tree(state, mark_dirty);
                event_dispatcher.set_root(root);
                needs_rebuild = false;
            }

            renderer.begin_frame();
            renderer.render_element_tree(root);
            renderer.end_frame();
        }

        vk_context.wait_idle();
        std::cout << "cgui exited cleanly.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << '\n';
        return 1;
    }
}
