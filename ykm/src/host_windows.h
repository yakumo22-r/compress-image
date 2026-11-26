#pragma once

#include "window_layer.h"
#include "system_window.h"

#include <memory>
#include <string>
#include <vector>

struct ImGuiContext;

#ifndef _WIN32
struct SDL_Texture;
#endif

#ifdef _WIN32
#include "d3d11_renderer.h"
#endif

namespace ykm {

class Session;

struct CanvasView {
    float canvas_w = 1;
    float canvas_h = 1;
    float scale = 1;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    float dpi_x = 1;
    float dpi_y = 1;
    int pixel_w = 1;
    int pixel_h = 1;
};

struct HostWindowRequest {
    Session* session = nullptr;
    std::string key;
    std::string title;
    float x = 0;
    float y = 0;
    float width = 480;
    float height = 320;
    int min_width = 0;
    int min_height = 0;
    bool has_position = false;
    WindowLayer layer = WindowLayer::Page;
};

struct HostWindow {
    Session* session = nullptr;
    std::string key;
    std::unique_ptr<SystemWindow> system;
    ImGuiContext* imgui = nullptr;
#ifndef _WIN32
    SDL_Texture* canvas = nullptr;
#endif
    WindowLayer layer = WindowLayer::Page;
    float zoom = 1.0f;
    int tex_w = 0;
    int tex_h = 0;
    CanvasView view;
    float last_zoom = 1.0f;
    bool seen = false;
#ifdef _WIN32
    D3D11WindowResources d3d;
    NativeSurface imgui_surface;
#endif
};

class HostWindows {
public:
    HostWindows() = default;
    ~HostWindows() { destroy_all(); }
    // Native retirement callbacks retain this owner's address.
    HostWindows(const HostWindows&) = delete;
    HostWindows& operator=(const HostWindows&) = delete;

    HostWindow* sync(const HostWindowRequest& request);
    HostWindow* find(uint32_t window_id);
    void close(uint32_t window_id);
    void begin_frame();
    void drop_unseen();
    void destroy_all();
    void apply_canvas_io(HostWindow& host);
    void present_canvas(HostWindow& host);
#ifdef _WIN32
    static bool sync_platform_surface(HostWindow& host);
#endif
    std::vector<HostWindow>& list() { return windows_; }
    const std::vector<HostWindow>& list() const { return windows_; }

private:
#ifdef _WIN32
    static void detach_platform_surface(HostWindow& host) noexcept;
#endif
    void destroy(HostWindow& host);
    void ensure_canvas(HostWindow& host);
    static CanvasView compute_view(const HostWindow& host);

    std::vector<HostWindow> windows_;
    struct ClosedWindow {
        Session* session;
        std::string key;
        bool seen;
    };
    std::vector<ClosedWindow> closed_windows_;
};

} // namespace ykm
