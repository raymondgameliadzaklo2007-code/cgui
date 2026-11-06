#include "core/window.hpp"
#include "renderer/vulkan_context.hpp"
#include "renderer/vulkan_2d.hpp"
#include "ui/element.hpp"
#include "ui/event_dispatcher.hpp"
#include "ui/event.hpp"
#include "ui/components/button.hpp"

#include <iostream>
#include <memory>
#include <vector>
#include <string>

namespace {

struct AppState {
    int click_counter{0};
    std::string status_message{"Click a button to test interactivity."};
};

std::shared_ptr<cgui::ui::Element> build_showcase(AppState& state, std::function<void()> mark_dirty) {
    auto root = cgui::ui::col();
    root->bg(cgui::ui::Color::hex(0x131417))
        .padding(32.0f)
        .gap(24.0f);

    // -----------------------------------------------------------------------
    // Header
    // -----------------------------------------------------------------------
    auto header = cgui::ui::col();
    header->gap(6.0f);

    auto badge = cgui::ui::row();
    badge->height(24.0f).padding_horizontal(10.0f).radius(12.0f)
        .bg(cgui::ui::Color::hex(0x1E293B)).align_items(YGAlignCenter);
    auto badge_text = cgui::ui::div();
    badge_text->text("GPUI COMPONENT STUDIO  •  STEP 1", cgui::ui::Color::hex(0x38BDF8)).font_size(11.0f);
    badge->add_child(badge_text);
    header->add_child(badge);

    auto title = cgui::ui::div();
    title->text("MSDF Font Rendering & Button Component", cgui::ui::Color::hex(0xF8FAFC)).font_size(28.0f);
    header->add_child(title);

    auto subtitle = cgui::ui::div();
    subtitle->text("Vector-sharp text across all display scales via Multi-channel Signed Distance Fields.",
                   cgui::ui::Color::hex(0x94A3B8)).font_size(14.0f);
    header->add_child(subtitle);

    root->add_child(header);

    // -----------------------------------------------------------------------
    // Section 1: Typography & Multi-scale MSDF Text Test
    // -----------------------------------------------------------------------
    auto card_type = cgui::ui::col();
    card_type->bg(cgui::ui::Color::hex(0x1E222A))
        .border(1.0f, cgui::ui::Color::hex(0x2D333F))
        .radius(10.0f)
        .padding(20.0f)
        .gap(12.0f);

    auto card_type_title = cgui::ui::div();
    card_type_title->text("1. MSDF FONT TYPOGRAPHY TEST (MULTIPLE SCALES)", cgui::ui::Color::hex(0x64748B)).font_size(12.0f);
    card_type->add_child(card_type_title);

    auto sample_42 = cgui::ui::div();
    sample_42->text("The Quick Brown Fox Jumps Over 123", cgui::ui::Color::hex(0xE2E8F0)).font_size(42.0f);
    card_type->add_child(sample_42);

    auto sample_24 = cgui::ui::div();
    sample_24->text("Crisp Vector Corners & Edges at 24px", cgui::ui::Color::hex(0x60A5FA)).font_size(24.0f);
    card_type->add_child(sample_24);

    auto sample_16 = cgui::ui::div();
    sample_16->text("Body text at 16px with sub-pixel screen derivative anti-aliasing.", cgui::ui::Color::hex(0xCBD5E1)).font_size(16.0f);
    card_type->add_child(sample_16);

    auto sample_12 = cgui::ui::div();
    sample_12->text("Micro-typography caption at 12px remains legible and clean.", cgui::ui::Color::hex(0x94A3B8)).font_size(12.0f);
    card_type->add_child(sample_12);

    root->add_child(card_type);

    // -----------------------------------------------------------------------
    // Section 2: Button Component Testing (Variants & Sizes)
    // -----------------------------------------------------------------------
    auto card_btn = cgui::ui::col();
    card_btn->bg(cgui::ui::Color::hex(0x1E222A))
        .border(1.0f, cgui::ui::Color::hex(0x2D333F))
        .radius(10.0f)
        .padding(20.0f)
        .gap(16.0f);

    auto card_btn_title = cgui::ui::div();
    card_btn_title->text("2. BUTTON COMPONENT (VARIANTS, SIZES & INTERACTIVE STATE)", cgui::ui::Color::hex(0x64748B)).font_size(12.0f);
    card_btn->add_child(card_btn_title);

    // Row of Variants
    auto row_variants = cgui::ui::row();
    row_variants->gap(12.0f).align_items(YGAlignCenter);

    auto btn_primary = cgui::ui::button("Primary Action")
        .primary()
        .on_click([&state, mark_dirty]() {
            state.click_counter++;
            state.status_message = "Clicked Primary Button! Total clicks: " + std::to_string(state.click_counter);
            mark_dirty();
        })
        .build();
    row_variants->add_child(btn_primary);

    auto btn_secondary = cgui::ui::button("Secondary")
        .secondary()
        .on_click([&state, mark_dirty]() {
            state.click_counter++;
            state.status_message = "Clicked Secondary Button!";
            mark_dirty();
        })
        .build();
    row_variants->add_child(btn_secondary);

    auto btn_ghost = cgui::ui::button("Ghost")
        .ghost()
        .on_click([&state, mark_dirty]() {
            state.click_counter++;
            state.status_message = "Clicked Ghost Button!";
            mark_dirty();
        })
        .build();
    row_variants->add_child(btn_ghost);

    auto btn_danger = cgui::ui::button("Danger")
        .danger()
        .on_click([&state, mark_dirty]() {
            state.click_counter = 0;
            state.status_message = "Reset click counter to 0.";
            mark_dirty();
        })
        .build();
    row_variants->add_child(btn_danger);

    auto btn_disabled = cgui::ui::button("Disabled")
        .disabled(true)
        .build();
    row_variants->add_child(btn_disabled);

    card_btn->add_child(row_variants);

    // Row of Sizes (Sm, Md, Lg)
    auto row_sizes = cgui::ui::row();
    row_sizes->gap(12.0f).align_items(YGAlignCenter);

    auto btn_sm = cgui::ui::button("Small (26px)").sm().secondary().build();
    row_sizes->add_child(btn_sm);

    auto btn_md = cgui::ui::button("Medium (34px)").md().secondary().build();
    row_sizes->add_child(btn_md);

    auto btn_lg = cgui::ui::button("Large (42px)").lg().secondary().build();
    row_sizes->add_child(btn_lg);

    card_btn->add_child(row_sizes);

    // Live Interaction Feedback Box
    auto feedback_box = cgui::ui::row();
    feedback_box->height(36.0f)
        .padding_horizontal(14.0f)
        .align_items(YGAlignCenter)
        .radius(6.0f)
        .bg(cgui::ui::Color::hex(0x131417))
        .border(1.0f, cgui::ui::Color::hex(0x2D333F));

    auto status_lbl = cgui::ui::div();
    status_lbl->text(state.status_message, cgui::ui::Color::hex(0x38BDF8)).font_size(13.0f);
    feedback_box->add_child(status_lbl);

    card_btn->add_child(feedback_box);

    root->add_child(card_btn);

    return root;
}

} // anonymous namespace

int main() {
    try {
        std::cout << "Starting cgui GPUI Component Studio (Step 1: MSDF Font & Button)...\n";

        cgui::core::Window window({
            .title = "cgui – GPUI Component Studio",
            .width = 1100,
            .height = 760,
            .resizable = true
        });

        cgui::renderer::VulkanContext vk_context(window.handle());
        cgui::renderer::Vulkan2DRenderer renderer(vk_context);

        AppState state{};
        bool needs_rebuild = true;
        auto mark_dirty = [&]() {
            needs_rebuild = true;
        };

        auto root = build_showcase(state, mark_dirty);
        cgui::ui::EventDispatcher event_dispatcher(root);

        // Responsive resize
        window.on_resize([&](int /*w*/, int /*h*/) {
            vk_context.recreate_swapchain();
            renderer.on_swapchain_resized();
        });

        // High-DPI cursor tracking
        window.on_mouse_move([&](double xpos, double ypos) {
            auto [fb_w, fb_h] = window.framebuffer_size();
            auto [win_w, win_h] = window.window_size();
            float scale_x = (win_w > 0) ? (static_cast<float>(fb_w) / win_w) : 1.0f;
            float scale_y = (win_h > 0) ? (static_cast<float>(fb_h) / win_h) : 1.0f;

            event_dispatcher.on_mouse_move(static_cast<float>(xpos) * scale_x,
                                           static_cast<float>(ypos) * scale_y);
        });

        // Click dispatching
        window.on_mouse_button([&](int button, int action, int /*mods*/) {
            auto [fb_w, fb_h] = window.framebuffer_size();
            auto [win_w, win_h] = window.window_size();
            float scale_x = (win_w > 0) ? (static_cast<float>(fb_w) / win_w) : 1.0f;
            float scale_y = (win_h > 0) ? (static_cast<float>(fb_h) / win_h) : 1.0f;

            double xpos = 0.0, ypos = 0.0;
            glfwGetCursorPos(window.handle(), &xpos, &ypos);

            cgui::ui::MouseButton mb = cgui::ui::MouseButton::Left;
            if (button == GLFW_MOUSE_BUTTON_RIGHT) mb = cgui::ui::MouseButton::Right;
            else if (button == GLFW_MOUSE_BUTTON_MIDDLE) mb = cgui::ui::MouseButton::Middle;

            event_dispatcher.on_mouse_button(mb, action == GLFW_PRESS,
                                            static_cast<float>(xpos) * scale_x,
                                            static_cast<float>(ypos) * scale_y);
        });

        std::cout << "Engine ready. Entering main event loop.\n";

        while (!window.should_close()) {
            window.poll_events();

            if (needs_rebuild) {
                root = build_showcase(state, mark_dirty);
                event_dispatcher.set_root(root);
                needs_rebuild = false;
            }

            renderer.begin_frame();
            renderer.render_element_tree(root);
            renderer.end_frame();
        }

        vk_context.wait_idle();
        std::cout << "Exited cleanly.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << '\n';
        return 1;
    }
}
