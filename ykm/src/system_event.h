#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ykm {

using SystemMessageHandler = bool (*)(void* user, void* window, uint32_t message,
                                      uintptr_t wparam, intptr_t lparam);

enum class SystemEventType {
    None,
    KeyDown,
    KeyUp,
    TextInput,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    WindowClose,
    WindowResize,
    WindowDpiChanged,
    WindowFocus,
    WindowEnterSizeMove,
    WindowExitSizeMove,
    FileDrop,
};

struct WindowFocusEvent {
    bool focused = false;
};

// UI content scale, not a framebuffer-size multiplier.
struct WindowScaleEvent {
    float x = 1.0f;
    float y = 1.0f;
};

struct SystemEvent {
    SystemEventType type = SystemEventType::None;
    uint32_t window_id = 0;
    WindowFocusEvent focus;
    WindowScaleEvent scale;
    uint32_t key = 0;
    uint32_t scancode = 0;
    uint32_t modifiers = 0;
    uint32_t character = 0;
    uint32_t mouse_buttons = 0;
    int button = 0;
    int width = 0;
    int height = 0;
    float x = 0;
    float y = 0;
    float delta_x = 0;
    float delta_y = 0;
    bool repeat = false;
    bool double_click = false;
    bool composing = false;
    std::vector<std::wstring> dropped_paths;
};

// Platform backends publish events here; the host consumes them once per frame.
void system_push_event(const SystemEvent& event);
std::optional<SystemEvent> system_poll_event();
void system_pump_messages();
void system_set_message_handler(SystemMessageHandler handler, void* user);
bool system_handle_message(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam);

// Called on the window thread during the native move/size modal loop.
using SystemFrameHandler = void (*)(void* user);
void system_set_frame_handler(SystemFrameHandler handler, void* user);
void system_request_frame();

} // namespace ykm
