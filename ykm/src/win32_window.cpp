#include "system_window.h"
#include "system_event.h"

#ifdef _WIN32

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include "app_permissions.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace ykm {
namespace {

constexpr wchar_t kWindowClassName[] = L"YkmNativeWindow";
constexpr UINT_PTR kSizeMoveTimer = 0x594b4d;
std::atomic<uint32_t> g_next_window_id{1};
std::once_flag g_class_once;

class Win32Window final : public SystemWindow {
public:
    explicit Win32Window(HWND hwnd, uint32_t id, const SystemWindowOptions& options)
        : hwnd_(hwnd), id_(id), layer_(options.layer),
          min_width_(options.min_width), min_height_(options.min_height) {}

    ~Win32Window() override {
        if (HWND hwnd = retire_surface()) DestroyWindow(hwnd);
    }

    uint32_t id() const override { return id_; }
    NativeSurface surface() const override { return {SurfaceKind::Win32Window, hwnd_}; }
    void set_surface_retiring_handler(SurfaceRetiringHandler handler, void* user) override {
        retiring_handler_ = handler;
        retiring_user_ = user;
    }
    HWND retire_surface() noexcept {
        HWND hwnd = hwnd_;
        hwnd_ = nullptr;
        if (hwnd) {
            KillTimer(hwnd, kSizeMoveTimer);
            if (retiring_handler_) retiring_handler_(retiring_user_, id_, {SurfaceKind::Win32Window, hwnd});
        }
        return hwnd;
    }
    WindowCapabilities capabilities() const override { return {true, true, true}; }
    float content_scale() const override {
        const UINT dpi = GetDpiForWindow(hwnd_);
        return dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
    }
    void* native_handle(NativeHandleKind kind) const override {
        return kind == NativeHandleKind::Window ? static_cast<void*>(hwnd_) : nullptr;
    }
    void set_title(const std::string& title) override {
        int size = MultiByteToWideChar(CP_UTF8, 0, title.data(), static_cast<int>(title.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(size), L'\0');
        if (size > 0) MultiByteToWideChar(CP_UTF8, 0, title.data(), static_cast<int>(title.size()), wide.data(), size);
        SetWindowTextW(hwnd_, wide.c_str());
    }
    SystemResult set_layer(WindowLayer layer) override {
        HWND insert_after = layer == WindowLayer::BG ? HWND_BOTTOM : HWND_NOTOPMOST;
        if (layer == WindowLayer::Modal) insert_after = HWND_TOP;
        else if (layer == WindowLayer::System || layer == WindowLayer::Debug) insert_after = HWND_TOPMOST;
        if (!SetWindowPos(hwnd_, insert_after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
            return SystemResult::Failed;
        layer_ = layer;
        return SystemResult::Ok;
    }
    SystemResult position(int& x, int& y) const override {
        x = y = 0;
        RECT rect{};
        if (!GetWindowRect(hwnd_, &rect)) return SystemResult::Failed;
        x = rect.left; y = rect.top;
        return SystemResult::Ok;
    }
    void client_size(int& width, int& height) const override {
        RECT rect{}; GetClientRect(hwnd_, &rect); width = rect.right - rect.left; height = rect.bottom - rect.top;
    }
    void framebuffer_size(int& width, int& height) const override {
        // Per-monitor-aware Win32 client coordinates are already physical pixels.
        client_size(width, height);
    }
    SystemResult maximize() override { return show(SW_MAXIMIZE); }
    SystemResult minimize() override { return show(SW_MINIMIZE); }
    SystemResult restore() override { return show(SW_RESTORE); }
    SystemResult show(int command) {
        if (!IsWindow(hwnd_)) return SystemResult::Failed;
        ShowWindow(hwnd_, command);
        return SystemResult::Ok;
    }

    void reset_text_input() { high_surrogate_ = 0; }
    void publish_character(uint32_t value, bool utf16) {
        auto publish = [this](uint32_t character) {
            SystemEvent event;
            event.type = SystemEventType::TextInput;
            event.window_id = id_;
            event.character = character;
            system_push_event(event);
        };
        if (utf16 && value >= 0xd800 && value <= 0xdbff) {
            if (high_surrogate_) publish(0xfffd);
            high_surrogate_ = value;
            return;
        }
        if (utf16 && value >= 0xdc00 && value <= 0xdfff && high_surrogate_) {
            publish(0x10000 + ((high_surrogate_ - 0xd800) << 10) + value - 0xdc00);
            high_surrogate_ = 0;
            return;
        }
        if (high_surrogate_) { publish(0xfffd); high_surrogate_ = 0; }
        publish(value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ? 0xfffd : value);
    }

    void begin_click(bool double_click) { double_click_ = double_click; }
    bool finish_click() { bool result = double_click_; double_click_ = false; return result; }
    void apply_minimum_size(MINMAXINFO& info) const {
        info.ptMinTrackSize.x = (std::max)(info.ptMinTrackSize.x, static_cast<LONG>(min_width_));
        info.ptMinTrackSize.y = (std::max)(info.ptMinTrackSize.y, static_cast<LONG>(min_height_));
    }
private:
    HWND hwnd_ = nullptr;
    uint32_t id_ = 0;
    WindowLayer layer_ = WindowLayer::Page;
    int min_width_ = 0;
    int min_height_ = 0;
    uint32_t high_surrogate_ = 0;
    bool double_click_ = false;
    SurfaceRetiringHandler retiring_handler_ = nullptr;
    void* retiring_user_ = nullptr;
};

uint32_t modifiers() {
    uint32_t result = 0;
    if (GetKeyState(VK_MENU) & 0x8000) result |= 1;
    if (GetKeyState(VK_CONTROL) & 0x8000) result |= 2;
    if (GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) result |= 4;
    if (GetKeyState(VK_SHIFT) & 0x8000) result |= 8;
    return result;
}

int mouse_button(UINT message, WPARAM wparam) {
    switch (message) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: return 0;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: return 1;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: return 2;
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
            return GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4;
        default: return -1;
    }
}

uint32_t mouse_buttons(WPARAM value) {
    return (value & MK_LBUTTON ? 1u : 0u) | (value & MK_RBUTTON ? 2u : 0u) |
           (value & MK_MBUTTON ? 4u : 0u) | (value & MK_XBUTTON1 ? 8u : 0u) |
           (value & MK_XBUTTON2 ? 16u : 0u);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (window && (message == WM_DESTROY || message == WM_NCDESTROY)) window->retire_surface();
    const bool imgui_handled = system_handle_message(
        hwnd, message, static_cast<uintptr_t>(wparam), static_cast<intptr_t>(lparam));
    const uint32_t id = window ? window->id() : 0;
    SystemEvent event;
    event.window_id = id;
    switch (message) {
        case WM_GETMINMAXINFO:
            DefWindowProcW(hwnd, message, wparam, lparam);
            if (window && lparam) window->apply_minimum_size(*reinterpret_cast<MINMAXINFO*>(lparam));
            return 0;
        case WM_DROPFILES: {
            const auto drop = reinterpret_cast<HDROP>(wparam);
            if (window && app_allows("desktop.filedrop") && app_allows("fs.data")) {
                event.type = SystemEventType::FileDrop;
                const UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
                for (UINT index = 0; index < count && index < 500; ++index) {
                    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                    if (!length || length > 32767) continue;
                    std::wstring path(length + 1, L'\0');
                    if (DragQueryFileW(drop, index, path.data(), length + 1) != length) continue;
                    path.resize(length);
                    event.dropped_paths.push_back(std::move(path));
                }
            }
            DragFinish(drop);
            if (event.type == SystemEventType::FileDrop) system_push_event(event);
            return 0;
        }
        case WM_CLOSE:
            event.type = SystemEventType::WindowClose;
            system_push_event(event);
            return 0;
        case WM_SIZE:
            event.type = SystemEventType::WindowResize;
            event.width = LOWORD(lparam);
            event.height = HIWORD(lparam);
            system_push_event(event);
            break;
        case WM_DPICHANGED:
            event.type = SystemEventType::WindowDpiChanged;
            event.scale.x = static_cast<float>(LOWORD(wparam)) / 96.0f;
            event.scale.y = static_cast<float>(HIWORD(wparam)) / 96.0f;
            system_push_event(event);
            if (lparam) {
                const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            break;
        case WM_SETFOCUS:
            event.type = SystemEventType::WindowFocus;
            event.focus.focused = true;
            system_push_event(event);
            break;
        case WM_KILLFOCUS:
            if (window) window->reset_text_input();
            if (window) window->begin_click(false);
            event.type = SystemEventType::WindowFocus;
            event.focus.focused = false;
            system_push_event(event);
            break;
        case WM_ENTERSIZEMOVE:
            SetTimer(hwnd, kSizeMoveTimer, 16, nullptr);
            event.type = SystemEventType::WindowEnterSizeMove;
            system_push_event(event);
            break;
        case WM_EXITSIZEMOVE:
            KillTimer(hwnd, kSizeMoveTimer);
            event.type = SystemEventType::WindowExitSizeMove;
            system_push_event(event);
            break;
        case WM_TIMER:
            if (wparam == kSizeMoveTimer) {
                if (window && window->surface()) system_request_frame();
                return 0;
            }
            break;
        case WM_NCDESTROY:
            KillTimer(hwnd, kSizeMoveTimer);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        case WM_CAPTURECHANGED:
            if (window) window->begin_click(false);
            break;
        case WM_CANCELMODE:
            if (window) window->begin_click(false);
            if (GetCapture() == hwnd) ReleaseCapture();
            break;
        case WM_MOUSEMOVE:
            event.type = SystemEventType::MouseMove;
            event.x = static_cast<float>(GET_X_LPARAM(lparam));
            event.y = static_cast<float>(GET_Y_LPARAM(lparam));
            event.modifiers = modifiers();
            event.mouse_buttons = mouse_buttons(wparam);
            system_push_event(event);
            {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
            }
            break;
        case WM_MOUSELEAVE:
            event.type = SystemEventType::MouseMove;
            event.x = -1;
            event.y = -1;
            event.modifiers = modifiers();
            system_push_event(event);
            break;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            event.type = SystemEventType::MouseWheel;
            if (message == WM_MOUSEHWHEEL)
                event.delta_x = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
            else
                event.delta_y = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
            event.modifiers = modifiers();
            event.mouse_buttons = mouse_buttons(wparam);
            {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(hwnd, &point);
                event.x = static_cast<float>(point.x);
                event.y = static_cast<float>(point.y);
            }
            system_push_event(event);
            break;
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONUP:
        case WM_MBUTTONUP: case WM_XBUTTONUP: case WM_LBUTTONDBLCLK:
            event.type = (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                          message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN ||
                          message == WM_LBUTTONDBLCLK)
                ? SystemEventType::MouseButtonDown : SystemEventType::MouseButtonUp;
            event.button = mouse_button(message, wparam);
            event.double_click = message == WM_LBUTTONDBLCLK;
            if (window && (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK))
                window->begin_click(event.double_click);
            if (window && message == WM_LBUTTONUP) event.double_click = window->finish_click();
            event.x = static_cast<float>(GET_X_LPARAM(lparam));
            event.y = static_cast<float>(GET_Y_LPARAM(lparam));
            event.modifiers = modifiers();
            event.mouse_buttons = mouse_buttons(wparam);
            if (event.type == SystemEventType::MouseButtonDown) SetCapture(hwnd);
            else if (event.mouse_buttons == 0 && GetCapture() == hwnd) ReleaseCapture();
            system_push_event(event);
            break;
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            event.type = SystemEventType::KeyDown;
            event.key = static_cast<uint32_t>(wparam);
            event.scancode = (lparam >> 16) & 0xff;
            if (lparam & (1ll << 24)) event.scancode |= 0x100;
            if (event.key == VK_CONTROL) event.key = (lparam & (1ll << 24)) ? VK_RCONTROL : VK_LCONTROL;
            if (event.key == VK_MENU) event.key = (lparam & (1ll << 24)) ? VK_RMENU : VK_LMENU;
            if (event.key == VK_SHIFT) event.key = event.scancode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
            event.modifiers = modifiers();
            event.repeat = (lparam & (1u << 30)) != 0;
            system_push_event(event);
            break;
        case WM_KEYUP: case WM_SYSKEYUP:
            event.type = SystemEventType::KeyUp;
            event.key = static_cast<uint32_t>(wparam);
            event.scancode = (lparam >> 16) & 0xff;
            if (lparam & (1ll << 24)) event.scancode |= 0x100;
            if (event.key == VK_CONTROL) event.key = (lparam & (1ll << 24)) ? VK_RCONTROL : VK_LCONTROL;
            if (event.key == VK_MENU) event.key = (lparam & (1ll << 24)) ? VK_RMENU : VK_LMENU;
            if (event.key == VK_SHIFT) event.key = event.scancode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
            event.modifiers = modifiers();
            system_push_event(event);
            break;
        case WM_CHAR:
            if (window) window->publish_character(static_cast<uint32_t>(wparam), true);
            break;
        case WM_UNICHAR:
            if (wparam == UNICODE_NOCHAR) return TRUE;
            if (window) window->publish_character(static_cast<uint32_t>(wparam), false);
            return TRUE;
            break;
        default:
            break;
    }
    return imgui_handled ? 0 : DefWindowProcW(hwnd, message, wparam, lparam);
}

void register_window_class() {
    std::call_once(g_class_once, [] {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSEXW klass{};
        klass.cbSize = sizeof(klass);
        klass.style = CS_DBLCLKS;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpfnWndProc = window_proc;
        klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
        klass.hIcon = LoadIconW(klass.hInstance, MAKEINTRESOURCEW(1));
        klass.hIconSm = static_cast<HICON>(LoadImageW(klass.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        klass.lpszClassName = kWindowClassName;
        RegisterClassExW(&klass);
    });
}

} // namespace

std::unique_ptr<SystemWindow> create_native_window(const SystemWindowOptions& options) {
    register_window_class();
    const int width = (std::max)(options.min_width, options.width > 0 ? static_cast<int>(options.width) : 480);
    const int height = (std::max)(options.min_height, options.height > 0 ? static_cast<int>(options.height) : 320);
    const int x = options.has_position ? static_cast<int>(options.x) : CW_USEDEFAULT;
    const int y = options.has_position ? static_cast<int>(options.y) : CW_USEDEFAULT;
    HWND hwnd = CreateWindowExW(0, kWindowClassName, L"YKM", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return nullptr;
    auto result = std::make_unique<Win32Window>(hwnd, g_next_window_id++, options);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(result.get()));
    result->set_title(options.title);
    result->set_layer(options.layer);
    if (app_allows("desktop.filedrop") && app_allows("fs.data")) DragAcceptFiles(hwnd, TRUE);
    return result;
}

} // namespace ykm

#else

namespace ykm {
std::unique_ptr<SystemWindow> create_native_window(const SystemWindowOptions&) { return nullptr; }
} // namespace ykm

#endif
