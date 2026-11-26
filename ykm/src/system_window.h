#pragma once

#include "window_layer.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ykm {

enum class NativeHandleKind {
    Window,
    Renderer,
};

enum class SurfaceKind { None, Win32Window, SdlWindow };

// Borrowed, window-thread-only descriptor. Valid until retirement; consumers
// that retain it for binding must release it in the retirement callback.
struct NativeSurface {
    SurfaceKind kind = SurfaceKind::None;
    void* handle = nullptr;
    // Increment when the native surface is replaced, even if its handle is reused.
    uint64_t generation = 1;
    explicit operator bool() const { return kind != SurfaceKind::None && handle != nullptr; }
    bool operator==(const NativeSurface& other) const noexcept {
        return kind == other.kind && handle == other.handle && generation == other.generation;
    }
    bool operator!=(const NativeSurface& other) const noexcept { return !(*this == other); }
};

struct SystemWindowOptions {
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

// Input coordinates use client units; render targets use framebuffer pixels.
// content_scale is a UI policy hint, not an extra framebuffer multiplier.
struct WindowMetrics {
    int client_width = 0;
    int client_height = 0;
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    float content_scale = 1.0f;
};

struct WindowCapabilities {
    bool desktop_position = false;
    bool window_states = false;
    bool layering = false;
};

// Ok means the platform accepted the request; completion may be asynchronous.
enum class SystemResult { Ok, Unsupported, Failed };

class SystemWindow {
public:
    virtual ~SystemWindow() = default;

    virtual uint32_t id() const = 0;
    virtual void* native_handle(NativeHandleKind kind) const = 0;
    virtual NativeSurface surface() const { return {}; }
    // Called synchronously on the window thread after withdrawal, while the
    // old native handle is still usable for cleanup. Do not destroy the window here.
    using SurfaceRetiringHandler = void (*)(void*, uint32_t, NativeSurface) noexcept;
    virtual void set_surface_retiring_handler(SurfaceRetiringHandler, void*) {}
    virtual void set_title(const std::string& title) = 0;
    virtual SystemResult set_layer(WindowLayer) { return SystemResult::Unsupported; }
    virtual SystemResult position(int& x, int& y) const {
        x = y = 0;
        return SystemResult::Unsupported;
    }
    virtual void client_size(int& width, int& height) const = 0;
    virtual void framebuffer_size(int& width, int& height) const = 0;
    virtual SystemResult maximize() { return SystemResult::Unsupported; }
    virtual SystemResult minimize() { return SystemResult::Unsupported; }
    virtual SystemResult restore() { return SystemResult::Unsupported; }
    virtual WindowCapabilities capabilities() const { return {}; }
    virtual float content_scale() const { return 1.0f; }
    WindowMetrics metrics() const {
        WindowMetrics result;
        client_size(result.client_width, result.client_height);
        framebuffer_size(result.framebuffer_width, result.framebuffer_height);
        result.content_scale = content_scale();
        return result;
    }
};

// Host-facing entry point. Backend selection belongs to sys, not UI/host.
std::unique_ptr<SystemWindow> create_system_window(const SystemWindowOptions& options);


} // namespace ykm
