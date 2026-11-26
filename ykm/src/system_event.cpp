#include "system_event.h"

#include <deque>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ykm {
namespace {
std::deque<SystemEvent> g_events;
std::mutex g_events_mutex;
SystemMessageHandler g_message_handler = nullptr;
void* g_message_handler_user = nullptr;
SystemFrameHandler g_frame_handler = nullptr;
void* g_frame_handler_user = nullptr;
}

void system_push_event(const SystemEvent& event) {
    std::lock_guard<std::mutex> lock(g_events_mutex);
    g_events.push_back(event);
}

std::optional<SystemEvent> system_poll_event() {
    std::lock_guard<std::mutex> lock(g_events_mutex);
    if (g_events.empty()) return std::nullopt;
    SystemEvent event = g_events.front();
    g_events.pop_front();
    return event;
}

void system_pump_messages() {
#ifdef _WIN32
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            SystemEvent event;
            event.type = SystemEventType::WindowClose;
            system_push_event(event);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
#endif
}

void system_set_message_handler(SystemMessageHandler handler, void* user) {
    g_message_handler = handler;
    g_message_handler_user = user;
}

bool system_handle_message(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam) {
    return g_message_handler && g_message_handler(g_message_handler_user, window, message, wparam, lparam);
}
void system_set_frame_handler(SystemFrameHandler handler, void* user) {
    g_frame_handler = handler;
    g_frame_handler_user = user;
}

void system_request_frame() {
    if (g_frame_handler) g_frame_handler(g_frame_handler_user);
}

} // namespace ykm
