#pragma once

#include <string>
#include <functional>
#include <memory>
#include <stdexcept>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace cgui::core {

struct WindowConfig {
    std::string title = "cgui – GPUI Studio";
    int width = 1280;
    int height = 800;
    bool resizable = true;
};

class Window {
public:
    explicit Window(const WindowConfig& config = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    [[nodiscard]] GLFWwindow* handle() const noexcept { return window_; }
    [[nodiscard]] bool should_close() const noexcept;
    void poll_events() const noexcept;
    void wait_events() const noexcept;

    [[nodiscard]] std::pair<int, int> framebuffer_size() const noexcept;
    [[nodiscard]] std::pair<int, int> window_size() const noexcept;

    using ResizeCallback = std::function<void(int width, int height)>;
    using CursorPosCallback = std::function<void(double x, double y)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;

    void on_resize(ResizeCallback callback) { on_resize_ = std::move(callback); }
    void on_mouse_move(CursorPosCallback callback) { on_mouse_move_ = std::move(callback); }
    void on_mouse_button(MouseButtonCallback callback) { on_mouse_button_ = std::move(callback); }

private:
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos);
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);

    GLFWwindow* window_{nullptr};
    ResizeCallback on_resize_;
    CursorPosCallback on_mouse_move_;
    MouseButtonCallback on_mouse_button_;
    static inline int window_count_{0};
};

} // namespace cgui::core
