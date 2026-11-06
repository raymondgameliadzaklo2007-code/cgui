#include "window.hpp"
#include <iostream>

namespace cgui::core {

Window::Window(const WindowConfig& config) {
    if (window_count_ == 0) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }
    }
    ++window_count_;

    // Tell GLFW not to create an OpenGL context since we use Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    window_ = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (!window_) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    glfwSetCursorPosCallback(window_, cursor_pos_callback);
    glfwSetMouseButtonCallback(window_, mouse_button_callback);
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

    --window_count_;
    if (window_count_ == 0) {
        glfwTerminate();
    }
}

Window::Window(Window&& other) noexcept : window_(other.window_), on_resize_(std::move(other.on_resize_)) {
    other.window_ = nullptr;
    if (window_) {
        glfwSetWindowUserPointer(window_, this);
    }
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        if (window_) {
            glfwDestroyWindow(window_);
            --window_count_;
        }
        window_ = other.window_;
        on_resize_ = std::move(other.on_resize_);
        other.window_ = nullptr;
        if (window_) {
            glfwSetWindowUserPointer(window_, this);
        }
    }
    return *this;
}

bool Window::should_close() const noexcept {
    return window_ ? glfwWindowShouldClose(window_) : true;
}

void Window::poll_events() const noexcept {
    glfwPollEvents();
}

void Window::wait_events() const noexcept {
    glfwWaitEvents();
}

std::pair<int, int> Window::framebuffer_size() const noexcept {
    int w = 0, h = 0;
    if (window_) {
        glfwGetFramebufferSize(window_, &w, &h);
    }
    return {w, h};
}

std::pair<int, int> Window::window_size() const noexcept {
    int w = 0, h = 0;
    if (window_) {
        glfwGetWindowSize(window_, &w, &h);
    }
    return {w, h};
}

void Window::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->on_resize_) {
        self->on_resize_(width, height);
    }
}

void Window::cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->on_mouse_move_) {
        self->on_mouse_move_(xpos, ypos);
    }
}

void Window::mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->on_mouse_button_) {
        self->on_mouse_button_(button, action, mods);
    }
}

} // namespace cgui::core
