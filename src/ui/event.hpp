#pragma once

#include <cstdint>
#include <string>

namespace cgui::ui {

enum class EventType {
    None,
    MouseMove,
    MouseDown,
    MouseUp,
    Scroll,
    KeyDown,
    KeyUp,
    CharInput
};

enum class MouseButton {
    Left = 0,
    Right = 1,
    Middle = 2,
    Other = 3
};

struct MouseMoveEvent {
    float x{0.0f};
    float y{0.0f};
    float dx{0.0f};
    float dy{0.0f};
};

struct MouseButtonEvent {
    MouseButton button{MouseButton::Left};
    float x{0.0f};
    float y{0.0f};
    bool is_down{false};
    int click_count{1};
};

struct ScrollEvent {
    float x{0.0f};
    float y{0.0f};
    float dx{0.0f};
    float dy{0.0f};
};

struct KeyEvent {
    int key{0};
    int scancode{0};
    int action{0}; // GLFW_PRESS, GLFW_RELEASE, GLFW_REPEAT
    int mods{0};
    bool is_down{false};
};

struct CharEvent {
    uint32_t codepoint{0};
};

class Event {
public:
    EventType type{EventType::None};
    bool handled{false};

    union {
        MouseMoveEvent mouse_move;
        MouseButtonEvent mouse_button;
        ScrollEvent scroll;
        KeyEvent key;
        CharEvent character;
    };

    Event() : type(EventType::None), handled(false), mouse_move{} {}
};

} // namespace cgui::ui
