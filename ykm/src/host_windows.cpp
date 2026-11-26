#include "host_windows.h"
#include "fonts.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <imgui.h>
#ifndef _WIN32
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#else
#include "d3d11_renderer.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#endif

namespace ykm {

#ifdef _WIN32
bool HostWindows::sync_platform_surface(HostWindow& host) {
    const NativeSurface surface = host.system->surface();
    if (surface && surface.kind == SurfaceKind::Win32Window && surface == host.imgui_surface)
        return true;
    detach_platform_surface(host);
    if (!surface || surface.kind != SurfaceKind::Win32Window) return false;

    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(host.imgui);
    const bool attached = ImGui_ImplWin32_Init(surface.handle);
    if (attached) host.imgui_surface = surface;
    ImGui::SetCurrentContext(previous);
    return attached;
}

void HostWindows::detach_platform_surface(HostWindow& host) noexcept {
    const NativeSurface old = host.imgui_surface;
    host.imgui_surface = {};
    host.d3d.detach(d3d11_renderer().context());
    if (!old) return;

    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(host.imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputKeys();
    ImGui::GetMainViewport()->PlatformHandle = nullptr;
    ImGui::GetMainViewport()->PlatformHandleRaw = nullptr;
    io.ClearInputMouse();
    ImGui_ImplWin32_Shutdown();
    ImGui::SetCurrentContext(previous);
}
#endif

HostWindow* HostWindows::sync(const HostWindowRequest& request) {
    for (auto& closed : closed_windows_) {
        if (closed.session == request.session && closed.key == request.key) {
            closed.seen = true;
            return nullptr;
        }
    }
    for (HostWindow& host : windows_) {
        if (host.session != request.session || host.key != request.key) continue;
        host.session = request.session;
        host.seen = true;
        host.system->set_title(request.title);
        if (host.layer != request.layer && host.system->capabilities().layering) {
            if (host.system->set_layer(request.layer) == SystemResult::Ok)
                host.layer = request.layer;
        }
        return &host;
    }

    SystemWindowOptions options;
    options.title = request.title;
    options.x = request.x;
    options.y = request.y;
    options.width = request.width;
    options.height = request.height;
    options.min_width = request.min_width;
    options.min_height = request.min_height;
    options.has_position = request.has_position;
    options.layer = request.layer;
    std::unique_ptr<SystemWindow> system = create_system_window(options);
    if (!system) return nullptr;
    const NativeSurface surface = system->surface();
    if (!surface) return nullptr;
#ifdef _WIN32
    if (surface.kind != SurfaceKind::Win32Window) return nullptr;
#else
    if (surface.kind != SurfaceKind::SdlWindow) return nullptr;
    SDL_Window* window = static_cast<SDL_Window*>(surface.handle);
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(system->native_handle(NativeHandleKind::Renderer));
#endif
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui);
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 10.0f;
    style.GrabRounding = 10.0f;
    style.PopupRounding = 12.0f;
    style.ScrollbarRounding = 10.0f;
    style.TabRounding = 10.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.PopupBorderSize = 1.0f;
    style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);
    style.ScrollbarSize = 14.0f;
    style.FramePadding = ImVec2(14.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.95f, 0.95f, 0.97f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_Text] = ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.56f, 0.58f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.00f, 0.478f, 1.00f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.10f, 0.53f, 1.00f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.40f, 0.90f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.93f, 0.93f, 0.95f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.90f, 0.93f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 0.478f, 1.00f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.478f, 1.00f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 0.40f, 0.90f, 1.0f);
    colors[ImGuiCol_Border] = ImVec4(0.84f, 0.84f, 0.86f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.90f, 0.90f, 0.93f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.85f, 0.90f, 1.00f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.478f, 1.00f, 0.35f);
    colors[ImGuiCol_PopupBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    fonts_load_ui_default();
    ui_zoom_capture_base();
#ifdef _WIN32
    if (!d3d11_renderer().initialize()) {
        ImGui::DestroyContext(imgui);
        ImGui::SetCurrentContext(previous);
        return nullptr;
    }
    if (!ImGui_ImplDX11_Init(d3d11_renderer().device(), d3d11_renderer().context())) {
        ImGui::DestroyContext(imgui);
        ImGui::SetCurrentContext(previous);
        return nullptr;
    }
#else
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
#endif
    if (previous) ImGui::SetCurrentContext(previous);

    HostWindow host;
    host.session = request.session;
    host.key = request.key;
    host.system = std::move(system);
    host.imgui = imgui;
    host.layer = WindowLayer::Page;
#ifdef _WIN32
    if (!sync_platform_surface(host)) {
        destroy(host);
        return nullptr;
    }
#endif
    host.seen = true;
    windows_.push_back(std::move(host));
#ifdef _WIN32
    windows_.back().system->set_surface_retiring_handler(
        [](void* user, uint32_t id, NativeSurface) noexcept {
            if (HostWindow* retiring = static_cast<HostWindows*>(user)->find(id))
                detach_platform_surface(*retiring);
        }, this);
#endif
    auto& inserted = windows_.back();
    if (inserted.system->capabilities().layering &&
        inserted.system->set_layer(request.layer) == SystemResult::Ok)
        inserted.layer = request.layer;
    return &inserted;
}

HostWindow* HostWindows::find(uint32_t window_id) {
    for (HostWindow& host : windows_) {
        if (host.system && host.system->id() == window_id) return &host;
    }
    return nullptr;
}

void HostWindows::close(uint32_t window_id) {
    for (size_t index = 0; index < windows_.size(); ++index) {
        if (windows_[index].system->id() != window_id) continue;
        closed_windows_.push_back({windows_[index].session, windows_[index].key, true});
        destroy(windows_[index]);
        windows_.erase(windows_.begin() + static_cast<ptrdiff_t>(index));
        return;
    }
}

void HostWindows::begin_frame() {
    for (HostWindow& host : windows_) host.seen = false;
    for (auto& closed : closed_windows_) closed.seen = false;
}

void HostWindows::drop_unseen() {
    closed_windows_.erase(std::remove_if(closed_windows_.begin(), closed_windows_.end(),
        [](const ClosedWindow& closed) { return !closed.seen; }), closed_windows_.end());
    for (int index = static_cast<int>(windows_.size()) - 1; index >= 0; --index) {
        if (windows_[static_cast<size_t>(index)].seen) continue;
        destroy(windows_[static_cast<size_t>(index)]);
        windows_.erase(windows_.begin() + index);
    }
}

void HostWindows::destroy_all() {
    for (HostWindow& host : windows_) destroy(host);
    windows_.clear();
    closed_windows_.clear();
}

void HostWindows::destroy(HostWindow& host) {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(host.imgui);
#ifdef _WIN32
    host.system->set_surface_retiring_handler(nullptr, nullptr);
    detach_platform_surface(host);
    ImGui_ImplDX11_Shutdown();
#else
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
#endif
    ImGui::DestroyContext(host.imgui);
    if (previous && previous != host.imgui) ImGui::SetCurrentContext(previous);
    else ImGui::SetCurrentContext(nullptr);
#ifndef _WIN32
    if (host.canvas) SDL_DestroyTexture(host.canvas);
    host.canvas = nullptr;
#endif
    host.imgui = nullptr;
    host.system.reset();
}

CanvasView HostWindows::compute_view(const HostWindow& host) {
    const WindowMetrics metrics = host.system->metrics();
    int window_w = metrics.client_width;
    int window_h = metrics.client_height;
    int pixel_win_w = metrics.framebuffer_width;
    int pixel_win_h = metrics.framebuffer_height;
    window_w = std::max(1, window_w);
    window_h = std::max(1, window_h);
    pixel_win_w = std::max(1, pixel_win_w);
    pixel_win_h = std::max(1, pixel_win_h);
    CanvasView view;
    view.canvas_w = static_cast<float>(window_w);
    view.canvas_h = static_cast<float>(window_h);
    view.scale = 1.0f;
    view.dpi_x = static_cast<float>(pixel_win_w) / static_cast<float>(window_w);
    view.dpi_y = static_cast<float>(pixel_win_h) / static_cast<float>(window_h);
    view.w = static_cast<float>(window_w);
    view.h = static_cast<float>(window_h);
    view.x = 0;
    view.y = 0;
    view.pixel_w = pixel_win_w;
    view.pixel_h = pixel_win_h;
    return view;
}

#ifndef _WIN32
void HostWindows::ensure_canvas(HostWindow& host) {
    const int width = std::max(1, host.view.pixel_w);
    const int height = std::max(1, host.view.pixel_h);
    if (host.canvas && host.tex_w == width && host.tex_h == height) return;
    if (host.canvas) SDL_DestroyTexture(host.canvas);
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(host.system->native_handle(NativeHandleKind::Renderer));
    host.canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
    if (host.canvas) SDL_SetTextureScaleMode(host.canvas, SDL_SCALEMODE_NEAREST);
    host.tex_w = width;
    host.tex_h = height;
}
#endif

void HostWindows::apply_canvas_io(HostWindow& host) {
    host.view = compute_view(host);
    ImGuiIO& io = ImGui::GetIO();
    const CanvasView& view = host.view;
    io.DisplaySize = ImVec2(static_cast<float>(view.pixel_w), static_cast<float>(view.pixel_h));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
#ifdef _WIN32
    const NativeSurface surface = host.system->surface();
    if (!surface || surface.kind != SurfaceKind::Win32Window) return;
    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(static_cast<HWND>(surface.handle), &point);
    io.AddMousePosEvent((point.x - view.x) * view.dpi_x, (point.y - view.y) * view.dpi_y);
#else
    float mx = 0;
    float my = 0;
    SDL_GetMouseState(&mx, &my);
    io.AddMousePosEvent((mx - view.x) * view.dpi_x, (my - view.y) * view.dpi_y);
#endif
}

void HostWindows::present_canvas(HostWindow& host) {
#ifdef _WIN32
    const NativeSurface surface = host.system->surface();
    if (!surface || surface != host.imgui_surface) return;
    int width = 0;
    int height = 0;
    host.system->framebuffer_size(width, height);
    if (!host.d3d.sync_surface(d3d11_renderer().device(), d3d11_renderer().context(),
                               surface, width, height)) return;
    const float clear[] = {0.949f, 0.949f, 0.969f, 1.0f};
    d3d11_renderer().context()->OMSetRenderTargets(1, &host.d3d.render_target, nullptr);
    d3d11_renderer().context()->ClearRenderTargetView(host.d3d.render_target, clear);
    if (ImDrawData* draw = ImGui::GetDrawData()) ImGui_ImplDX11_RenderDrawData(draw);
    host.d3d.swap_chain->Present(1, 0);
#else
    ensure_canvas(host);
    ImDrawData* draw = ImGui::GetDrawData();
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(host.system->native_handle(NativeHandleKind::Renderer));
    SDL_SetRenderDrawColor(renderer, 242, 242, 247, 255);
    SDL_RenderClear(renderer);
    if (host.canvas && draw) {
        SDL_SetRenderTarget(renderer, host.canvas);
        SDL_SetRenderDrawColor(renderer, 242, 242, 247, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(draw, renderer);
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_FRect dest{host.view.x, host.view.y, host.view.w, host.view.h};
        SDL_RenderTexture(renderer, host.canvas, nullptr, &dest);
    } else if (draw) {
        ImGui_ImplSDLRenderer3_RenderDrawData(draw, renderer);
    }
    SDL_RenderPresent(renderer);
#endif
}

} // namespace ykm
