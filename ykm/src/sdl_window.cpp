#include "system_window.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ykm {
namespace {

class SdlWindow final : public SystemWindow {
public:
    SdlWindow(SDL_Window* window, SDL_Renderer* renderer, WindowLayer layer)
        : window_(window), renderer_(renderer), layer_(layer) {}
    ~SdlWindow() override;

    uint32_t id() const override;
    NativeSurface surface() const override { return {SurfaceKind::SdlWindow, window_}; }
    void* native_handle(NativeHandleKind kind) const override;
    void set_title(const std::string& title) override;
    SystemResult set_layer(WindowLayer layer) override;
    SystemResult position(int& x, int& y) const override;
    void client_size(int& width, int& height) const override;
    void framebuffer_size(int& width, int& height) const override;
    SystemResult maximize() override;
    SystemResult minimize() override;
    SystemResult restore() override;

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    WindowLayer layer_ = WindowLayer::Page;
};

#ifdef _WIN32
HWND win32_hwnd(SDL_Window* window) {
    return static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}
#endif

} // namespace

std::unique_ptr<SystemWindow> create_sdl_window(const SystemWindowOptions& options) {
    SDL_Window* window = SDL_CreateWindow(
        options.title.c_str(),
        static_cast<int>(options.width > 0 ? options.width : 480),
        static_cast<int>(options.height > 0 ? options.height : 320),
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) return nullptr;
    if (options.has_position) {
        SDL_SetWindowPosition(window, static_cast<int>(options.x), static_cast<int>(options.y));
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_DestroyWindow(window);
        return nullptr;
    }
    auto result = std::make_unique<SdlWindow>(window, renderer, options.layer);
    result->set_layer(options.layer);
    return result;
}

SdlWindow::~SdlWindow() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

uint32_t SdlWindow::id() const {
    return SDL_GetWindowID(window_);
}

void* SdlWindow::native_handle(NativeHandleKind kind) const {
    return kind == NativeHandleKind::Window ? static_cast<void*>(window_) : static_cast<void*>(renderer_);
}

void SdlWindow::set_title(const std::string& title) {
    SDL_SetWindowTitle(window_, title.c_str());
}

SystemResult SdlWindow::set_layer(WindowLayer layer) {
#ifdef _WIN32
    HWND hwnd = win32_hwnd(window_);
    if (!hwnd) return SystemResult::Failed;
    HWND insert_after = HWND_NOTOPMOST;
    if (layer == WindowLayer::BG) insert_after = HWND_BOTTOM;
    else if (layer == WindowLayer::Modal) insert_after = HWND_TOP;
    else if (layer == WindowLayer::System || layer == WindowLayer::Debug) insert_after = HWND_TOPMOST;
    if (!SetWindowPos(hwnd, insert_after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
        return SystemResult::Failed;
#else
    if (layer == WindowLayer::BG || layer == WindowLayer::Modal) return SystemResult::Unsupported;
    const bool topmost = layer == WindowLayer::System || layer == WindowLayer::Debug;
    if (!SDL_SetWindowAlwaysOnTop(window_, topmost)) return SystemResult::Failed;
#endif
    layer_ = layer;
    return SystemResult::Ok;
}

SystemResult SdlWindow::position(int& x, int& y) const {
    x = y = 0;
    if (SDL_GetWindowPosition(window_, &x, &y)) return SystemResult::Ok;
    x = y = 0;
    return SystemResult::Failed;
}

void SdlWindow::client_size(int& width, int& height) const {
    SDL_GetWindowSize(window_, &width, &height);
}

void SdlWindow::framebuffer_size(int& width, int& height) const {
    SDL_GetWindowSizeInPixels(window_, &width, &height);
}

SystemResult SdlWindow::maximize() {
    return SDL_MaximizeWindow(window_) ? SystemResult::Ok : SystemResult::Failed;
}

SystemResult SdlWindow::minimize() {
    return SDL_MinimizeWindow(window_) ? SystemResult::Ok : SystemResult::Failed;
}

SystemResult SdlWindow::restore() {
    return SDL_RestoreWindow(window_) ? SystemResult::Ok : SystemResult::Failed;
}

} // namespace ykm
