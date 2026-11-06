#include "core/window.hpp"
#include "renderer/vulkan_context.hpp"
#include "renderer/vulkan_2d.hpp"
#include "ui/element.hpp"

#include <iostream>
#include <memory>

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

        window.on_resize([&](int /*w*/, int /*h*/) {
            vk_context.recreate_swapchain();
            renderer.on_swapchain_resized();
        });

        // 3. Construct GPUI-style Declarative Layout Tree using Yoga
        auto root = cgui::ui::col();
        root->width(1280.0f).height(800.0f).bg(cgui::ui::Color::hex(0x181A1F));

        // Header / Main Toolbar
        auto toolbar = cgui::ui::row();
        toolbar->height(38.0f)
            .padding_horizontal(16.0f)
            .align_items(YGAlignCenter)
            .justify_content(YGJustifySpaceBetween)
            .bg(cgui::ui::Color::hex(0x21252B))
            .border(1.0f, cgui::ui::Color::hex(0x181A1F));

        auto title = cgui::ui::div();
        title->text("cgui GPUI Editor", cgui::ui::Color::hex(0xABB2BF));
        toolbar->add_child(title);
        root->add_child(toolbar);

        // Body: Sidebar + Main Editor Area
        auto body = cgui::ui::row();
        body->flex_grow(1.0f);

        auto sidebar = cgui::ui::col();
        sidebar->width(240.0f)
            .bg(cgui::ui::Color::hex(0x1E2227))
            .border(1.0f, cgui::ui::Color::hex(0x181A1F))
            .padding(12.0f);
        body->add_child(sidebar);

        auto editor_area = cgui::ui::col();
        editor_area->flex_grow(1.0f)
            .bg(cgui::ui::Color::hex(0x282C34))
            .padding(16.0f);
        body->add_child(editor_area);

        root->add_child(body);

        // Status Bar
        auto status_bar = cgui::ui::row();
        status_bar->height(26.0f)
            .padding_horizontal(12.0f)
            .align_items(YGAlignCenter)
            .bg(cgui::ui::Color::hex(0x21252B));
        root->add_child(status_bar);

        std::cout << "Engine initialized. Entering main event loop.\n";

        // Main Loop
        while (!window.should_close()) {
            window.poll_events();

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
