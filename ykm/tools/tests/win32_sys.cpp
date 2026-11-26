#include "system_window.h"
#include "system_event.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <vector>

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static std::vector<ykm::SystemEvent> drain() {
    std::vector<ykm::SystemEvent> result;
    while (auto event = ykm::system_poll_event()) result.push_back(*event);
    return result;
}
static void text(HWND hwnd, UINT message, WPARAM value) {
    SendMessageW(hwnd, message, value, 0);
}
static void expect_text(std::initializer_list<uint32_t> expected) {
    std::vector<uint32_t> actual;
    for (const auto& event : drain()) {
        if (event.type != ykm::SystemEventType::TextInput) continue;
        check(!event.composing, "committed character incorrectly marked composing");
        actual.push_back(event.character);
    }
    check(actual == std::vector<uint32_t>(expected), "Unicode scalar sequence mismatch");
}
class MinimalWindow final : public ykm::SystemWindow {
public:
    uint32_t id() const override { return 0; }
    void* native_handle(ykm::NativeHandleKind) const override { return nullptr; }
    void set_title(const std::string&) override {}
    void client_size(int& w, int& h) const override { w = h = 0; }
    void framebuffer_size(int& w, int& h) const override { w = h = 0; }
};

int main() {
    try {
        MinimalWindow minimal;
        check(!minimal.surface(), "minimal backend must expose no native surface");
        check(!minimal.capabilities().window_states &&
              minimal.maximize() == ykm::SystemResult::Unsupported &&
              minimal.minimize() == ykm::SystemResult::Unsupported &&
              minimal.restore() == ykm::SystemResult::Unsupported, "optional window states contract failed");
        int unsupported_x = 123, unsupported_y = 456;
        check(minimal.position(unsupported_x, unsupported_y) == ykm::SystemResult::Unsupported &&
              unsupported_x == 0 && unsupported_y == 0, "unsupported position must clear outputs");
        check(minimal.set_layer(ykm::WindowLayer::Page) == ykm::SystemResult::Unsupported,
              "optional layering contract failed");
        ykm::SystemWindowOptions options;
        options.title = "YKM sys regression";
        auto window = ykm::create_system_window(options);
        check(window != nullptr, "window creation failed");
        const auto surface = window->surface();
        check(surface && surface.kind == ykm::SurfaceKind::Win32Window, "Win32 surface descriptor missing");
        auto hwnd = static_cast<HWND>(surface.handle);
        const auto capabilities = window->capabilities();
        check(capabilities.desktop_position && capabilities.window_states && capabilities.layering,
              "Win32 capabilities missing");
        check(window->content_scale() == static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f,
              "content scale query mismatch");
        int window_x = 0, window_y = 0;
        RECT window_rect{};
        check(GetWindowRect(hwnd, &window_rect) &&
              window->position(window_x, window_y) == ykm::SystemResult::Ok &&
              window_x == window_rect.left && window_y == window_rect.top, "position query failed");
        check(window->set_layer(ykm::WindowLayer::Page) == ykm::SystemResult::Ok, "layer request failed");
        check(window->maximize() == ykm::SystemResult::Ok && IsZoomed(hwnd), "maximize request failed");
        check(window->minimize() == ykm::SystemResult::Ok && IsIconic(hwnd), "minimize request failed");
        check(window->restore() == ykm::SystemResult::Ok && !IsIconic(hwnd), "restore request failed");
        for (int i = 0; i < 3; ++i) {
            check(SetWindowPos(hwnd, nullptr, 0, 0, 500 + i * 50, 350 + i * 30,
                               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0, "resize failed");
            RECT rect{};
            check(GetClientRect(hwnd, &rect) != 0, "GetClientRect failed");
            int width = 0, height = 0;
            window->framebuffer_size(width, height);
            check(width == rect.right && height == rect.bottom, "framebuffer must equal client pixels");
            const auto metrics = window->metrics();
            check(metrics.client_width == width && metrics.client_height == height &&
                  metrics.framebuffer_width == width && metrics.framebuffer_height == height,
                  "unified metrics mismatch");
        }
        std::cout << "client/framebuffer pixels: OK; DPI=" << GetDpiForWindow(hwnd) << '\n';
        drain();
        SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
        SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(144, 192), 0);
        int focus_events = 0;
        bool scale_event = false;
        for (const auto& event : drain()) {
            if (event.type == ykm::SystemEventType::WindowFocus) {
                check(event.focus.focused == (focus_events == 0), "focus payload mismatch");
                check(!event.repeat, "focus leaked into key repeat");
                ++focus_events;
            }
            if (event.type == ykm::SystemEventType::WindowDpiChanged) {
                scale_event = event.scale.x == 1.5f && event.scale.y == 2.0f;
                check(event.width == 0 && event.height == 0, "DPI leaked into window dimensions");
            }
        }
        check(focus_events == 2 && scale_event, "missing typed window events");
        text(hwnd, WM_CHAR, 0x4e2d);
        text(hwnd, WM_CHAR, 0xd83d);
        expect_text({0x4e2d});
        text(hwnd, WM_CHAR, 0xde00);
        expect_text({0x1f600});
        text(hwnd, WM_CHAR, 0xdc00);
        text(hwnd, WM_CHAR, 0xd800);
        text(hwnd, WM_CHAR, 'A');
        expect_text({0xfffd, 0xfffd, 'A'});
        text(hwnd, WM_CHAR, 0xd800);
        text(hwnd, WM_KILLFOCUS, 0);
        text(hwnd, WM_CHAR, 0xdc00);
        expect_text({0xfffd});
        check(SendMessageW(hwnd, WM_UNICHAR, UNICODE_NOCHAR, 0) == TRUE, "WM_UNICHAR query failed");
        text(hwnd, WM_UNICHAR, 0x1f680);
        text(hwnd, WM_UNICHAR, 0x110000);
        expect_text({0x1f680, 0xfffd});
        SendMessageW(hwnd, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(12, 20));
        bool double_click = false;
        for (const auto& event : drain()) {
            if (event.type == ykm::SystemEventType::MouseButtonDown) {
                double_click = event.double_click && event.button == 0;
            }
        }
        check(double_click, "left double click button mismatch");
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(12, 20));
        bool double_release = false;
        for (const auto& event : drain()) {
            if (event.type == ykm::SystemEventType::MouseButtonUp)
                double_release = event.double_click && event.button == 0;
        }
        check(double_release, "double click flag missing on release");
        check((GetClassLongPtrW(hwnd, GCL_STYLE) & CS_DBLCLKS) != 0, "native double clicks disabled");
        SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON | MK_XBUTTON2, MAKELPARAM(10, 20));
        SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(MK_RBUTTON | MK_MBUTTON, WHEEL_DELTA), 0);
        bool move_buttons = false, wheel_buttons = false;
        for (const auto& event : drain()) {
            if (event.type == ykm::SystemEventType::MouseMove) move_buttons = event.mouse_buttons == 17;
            if (event.type == ykm::SystemEventType::MouseWheel) wheel_buttons = event.mouse_buttons == 6;
        }
        check(move_buttons && wheel_buttons, "motion/wheel button mask mismatch");
        for (int direction : {1, -1}) {
            SendMessageW(hwnd, WM_MOUSEHWHEEL, MAKEWPARAM(MK_LBUTTON, direction * WHEEL_DELTA), 0);
            bool horizontal = false;
            for (const auto& event : drain()) {
                if (event.type == ykm::SystemEventType::MouseWheel)
                    horizontal = event.delta_x == direction && event.delta_y == 0 && event.mouse_buttons == 1;
            }
            check(horizontal, "horizontal wheel direction/button mismatch");
        }
        for (UINT cancel : {WM_CAPTURECHANGED, WM_CANCELMODE, WM_KILLFOCUS}) {
            SendMessageW(hwnd, WM_LBUTTONDBLCLK, MK_LBUTTON, 0);
            drain();
            SendMessageW(hwnd, cancel, 0, 0);
            if (cancel == WM_CANCELMODE) check(GetCapture() != hwnd, "cancel mode retained capture");
            SendMessageW(hwnd, WM_LBUTTONUP, 0, 0);
            bool released = false;
            for (const auto& event : drain()) {
                if (event.type == ykm::SystemEventType::MouseButtonUp) {
                    check(!event.double_click, "cancelled double click leaked into release");
                    released = true;
                }
            }
            check(released, "missing release after cancellation");
        }
        std::cout << "Horizontal wheel and capture cancellation: OK\n";
        std::cout << "Unicode, focus reset, double click: OK\n";
        for (bool external : {false, true}) {
            struct Retirement {
                ykm::SystemWindow* window;
                uint32_t id;
                ykm::NativeSurface surface;
                int calls = 0;
                bool correct = false;
            };
            Retirement state{window.get(), window->id(), window->surface()};
            window->set_surface_retiring_handler([](void* user, uint32_t id, ykm::NativeSurface old) noexcept {
                auto& state = *static_cast<Retirement*>(user);
                ++state.calls;
                state.correct = id == state.id && old == state.surface && !state.window->surface() &&
                                IsWindow(static_cast<HWND>(old.handle));
            }, &state);
            const HWND old = static_cast<HWND>(state.surface.handle);
            if (external) {
                const bool destroyed = DestroyWindow(old) != FALSE;
                const bool withdrawn = !window->surface();
                window.reset();
                check(destroyed && withdrawn, "external destruction left a published surface");
            } else {
                window.reset();
            }
            check(state.calls == 1 && state.correct && !IsWindow(old), "surface retirement order/count mismatch");
            if (!external) {
                window = ykm::create_system_window(options);
                check(window != nullptr, "second window creation failed");
            }
        }
        std::cout << "Owned and external surface retirement: OK\n";
        drain();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
