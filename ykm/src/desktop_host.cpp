#include "desktop_host.h"

#include "app_storage.h"
#include "debug_console.h"
#include "fonts.h"
#include "host_windows.h"
#include "session.h"
#include "sys_api.h"
#include "ui_imgui_api.h"
#include "ui_renderer.h"
#include "window_events.h"
#include "system_event.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <objbase.h>
#endif

#ifndef _WIN32
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#endif
#ifdef _WIN32
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "d3d11_renderer.h"
#include <windows.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#endif
#include <imgui.h>

#include <ykm/embedded_app.h>

namespace {

#ifndef _WIN32
ykm::SystemEvent system_event_from_sdl(const SDL_Event& source) {
    ykm::SystemEvent event;
    switch (source.type) {
        case SDL_EVENT_KEY_DOWN:
            event.type = ykm::SystemEventType::KeyDown;
            event.window_id = source.key.windowID;
            event.key = static_cast<uint32_t>(source.key.key);
            event.scancode = static_cast<uint32_t>(source.key.scancode);
            event.modifiers = static_cast<uint32_t>(source.key.mod);
            event.repeat = source.key.repeat;
            break;
        case SDL_EVENT_KEY_UP:
            event.type = ykm::SystemEventType::KeyUp;
            event.window_id = source.key.windowID;
            event.key = static_cast<uint32_t>(source.key.key);
            event.scancode = static_cast<uint32_t>(source.key.scancode);
            event.modifiers = static_cast<uint32_t>(source.key.mod);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            event.type = ykm::SystemEventType::MouseMove;
            event.window_id = source.motion.windowID;
            event.x = source.motion.x;
            event.y = source.motion.y;
            event.delta_x = source.motion.xrel;
            event.delta_y = source.motion.yrel;
            event.modifiers = static_cast<uint32_t>(source.motion.state);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            event.type = source.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                ? ykm::SystemEventType::MouseButtonDown : ykm::SystemEventType::MouseButtonUp;
            event.window_id = source.button.windowID;
            event.x = source.button.x;
            event.y = source.button.y;
            event.button = source.button.button == SDL_BUTTON_LEFT ? 0
                : source.button.button == SDL_BUTTON_MIDDLE ? 1
                : source.button.button == SDL_BUTTON_RIGHT ? 2
                : source.button.button == SDL_BUTTON_X1 ? 3
                : source.button.button == SDL_BUTTON_X2 ? 4 : -1;
            event.repeat = source.button.clicks > 1;
            event.modifiers = static_cast<uint32_t>(SDL_GetMouseState(nullptr, nullptr));
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            event.type = ykm::SystemEventType::MouseWheel;
            event.window_id = source.wheel.windowID;
            event.x = source.wheel.mouse_x;
            event.y = source.wheel.mouse_y;
            event.delta_x = source.wheel.x;
            event.delta_y = source.wheel.y;
            event.modifiers = static_cast<uint32_t>(SDL_GetMouseState(nullptr, nullptr));
            break;
        default:
            break;
    }
    return event;
}
#endif

void install_default_api_units(ykm::SessionManager& manager) {
    manager.add_api_unit(ykm::sys_api_unit());
    manager.add_api_unit(ykm::ui_imgui_api_unit());
}

#ifdef _WIN32
bool imgui_system_message(void* user, void* raw_window, uint32_t message,
                          uintptr_t wparam, intptr_t lparam) {
    auto* hosts = static_cast<ykm::HostWindows*>(user);
    ykm::HostWindow* host_window = nullptr;
    for (auto& candidate : hosts->list()) {
        if (candidate.system && candidate.imgui && candidate.imgui_surface &&
            candidate.imgui_surface.handle == raw_window &&
            candidate.system->surface() == candidate.imgui_surface) {
            host_window = &candidate;
            break;
        }
    }
    if (!host_window) return false;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(host_window->imgui);
    const bool handled = ImGui_ImplWin32_WndProcHandler(
        static_cast<HWND>(raw_window), message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam)) != 0;
    ImGui::SetCurrentContext(previous);
    return handled;
}
#endif

std::string json_string_field(const std::string& source, const char* field) {
    const std::regex pattern(std::string("\\\"") + field + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    return std::regex_search(source, match, pattern) ? match[1].str() : std::string{};
}

std::string json_repository_field(const std::string& source) {
    std::string repository = json_string_field(source, "repository");
    if (!repository.empty()) return repository;
    const std::regex object_pattern(
        "\\\"repository\\\"\\s*:\\s*\\{[^}]*\\\"url\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    return std::regex_search(source, match, object_pattern) ? match[1].str() : std::string{};
}

} // namespace

int run_desktop_host(const std::filesystem::path& widget_root) {
#ifndef _WIN32
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }
#endif
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    IMGUI_CHECKVERSION();
    ykm::HostWindows hosts;
#ifdef _WIN32
    ykm::system_set_message_handler(imgui_system_message, &hosts);
#endif
    ykm::SessionManager sessions;
    install_default_api_units(sessions);
    sessions.set_root(std::filesystem::absolute(widget_root));
    bool loaded = false;
    const YkmEmbeddedApp* embedded = ykm_get_embedded_app();
    if (embedded && embedded->struct_size >= sizeof(YkmEmbeddedApp)) {
        ykm::load_embedded_permissions();
        for (uint32_t index = 0; index < embedded->session_count; ++index) {
            const YkmEmbeddedSession& entry = embedded->sessions[index];
            loaded |= sessions.register_session({entry.name, entry.role, entry.module, true});
            if (entry.autostart) sessions.start(entry.name);
        }
        sessions.start_pending();
    }
    const std::filesystem::path registry = widget_root / "ykm.sessions";
    if (!embedded && std::filesystem::exists(registry)) {
        ykm::load_disk_permissions(widget_root);
        std::ifstream input(registry);
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream fields(line);
            std::string name;
            std::string role;
            std::string autostart;
            std::string script;
            if (std::getline(fields, name, '\t') && std::getline(fields, role, '\t') &&
                std::getline(fields, autostart, '\t') && std::getline(fields, script)) {
                loaded |= sessions.register_session({name, role, (widget_root / script).string()});
                if (autostart == "1") sessions.start(name);
            }
        }
        sessions.start_pending();
    } else if (!embedded && std::filesystem::exists(widget_root)) {
        for (const auto& entry : std::filesystem::directory_iterator(widget_root)) {
            const std::filesystem::path script = entry.path() / "main.js";
            if (entry.is_directory() && std::filesystem::exists(script)) {
                loaded |= sessions.add(entry.path().filename().string(), script.string());
            }
        }
    }
    if (!loaded) {
        std::cerr << "No widgets loaded from " << widget_root.string() << '\n';
    }

    ykm::UiRenderer ui;
    if (embedded && embedded->struct_size >= sizeof(YkmEmbeddedApp)) {
        ui.set_app_metadata(
            embedded->id ? embedded->id : "",
            embedded->version ? embedded->version : "",
            embedded->repository ? embedded->repository : "");
    } else {
        std::ifstream manifest_input(widget_root / "ykm.manifest.json");
        std::stringstream manifest_buffer;
        manifest_buffer << manifest_input.rdbuf();
        const std::string manifest = manifest_buffer.str();
        ui.set_app_metadata(
            json_string_field(manifest, "id"),
            json_string_field(manifest, "version"),
            json_repository_field(manifest));
    }
#ifdef YKM_DEBUG_CONSOLE
    ykm::DebugConsole debug_console;
#endif
    bool running = true;
    bool frame_active = false;
    auto run_frame = [&](bool modal) {
        if (!running || frame_active) return;
        frame_active = true;
        struct FrameGuard {
            bool& active;
            ~FrameGuard() { active = false; }
        } guard{frame_active};
        ImGuiContext* previous_context = ImGui::GetCurrentContext();
        if (!modal) {
        #ifdef _WIN32
        while (const auto event = ykm::system_poll_event()) {
            if (event->type == ykm::SystemEventType::WindowClose && event->window_id == 0) {
                running = false;
                continue;
            }
            ykm::HostWindow* host = event->window_id ? hosts.find(event->window_id) : nullptr;
            if (!host) continue;
            ImGui::SetCurrentContext(host->imgui);
            if (event->type == ykm::SystemEventType::KeyDown && event->key == VK_F12 && !event->repeat) {
#ifdef YKM_DEBUG_CONSOLE
                debug_console.toggle();
#endif
            }
            if (event->type == ykm::SystemEventType::WindowClose) {
                const uint32_t window_id = event->window_id;
                hosts.close(window_id);
                if (hosts.list().empty()) running = false;
            } else if (event->type == ykm::SystemEventType::WindowResize ||
                       event->type == ykm::SystemEventType::WindowDpiChanged ||
                       event->type == ykm::SystemEventType::WindowEnterSizeMove ||
                       event->type == ykm::SystemEventType::WindowExitSizeMove ||
                       event->type == ykm::SystemEventType::WindowFocus) {
                // HostWindows reads the authoritative client/framebuffer size before each frame.
                // These events intentionally only invalidate the current frame's platform state.
            } else if (host->session && (event->type == ykm::SystemEventType::KeyDown ||
                       event->type == ykm::SystemEventType::KeyUp ||
                       event->type == ykm::SystemEventType::TextInput ||
                       event->type == ykm::SystemEventType::MouseMove ||
                       event->type == ykm::SystemEventType::MouseButtonDown ||
                       event->type == ykm::SystemEventType::MouseButtonUp ||
                       event->type == ykm::SystemEventType::MouseWheel ||
                       event->type == ykm::SystemEventType::FileDrop)) {
                ykm::InputViewport viewport{host->view.x, host->view.y, host->view.scale};
                ykm::window_events_dispatch(*host->session, *host->system, viewport, *event);
            }
        }
        #else
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            for (const auto& host : hosts.list()) {
                ImGui::SetCurrentContext(host.imgui);
                ImGui_ImplSDL3_ProcessEvent(&event);
            }
            uint32_t event_window_id = 0;
            switch (event.type) {
                case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: event_window_id = event.key.windowID; break;
                case SDL_EVENT_MOUSE_MOTION: event_window_id = event.motion.windowID; break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: event_window_id = event.button.windowID; break;
                case SDL_EVENT_MOUSE_WHEEL: event_window_id = event.wheel.windowID; break;
                default: break;
            }
            if (event_window_id != 0) {
                ykm::HostWindow* host = hosts.find(event_window_id);
                bool zoom_shortcut = false;
                if (host && event.type == SDL_EVENT_MOUSE_WHEEL &&
                    (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
                    float wheel = event.wheel.y;
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) wheel = -wheel;
                    if (wheel != 0.0f) {
                        host->zoom = std::clamp(
                            host->zoom * std::pow(1.025f, wheel), 0.5f, 2.0f);
                        zoom_shortcut = true;
                    }
                } else if (host && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                           (event.key.mod & SDL_KMOD_CTRL) != 0) {
                    const bool zoom_in = event.key.key == SDLK_PLUS || event.key.key == SDLK_EQUALS ||
                                         event.key.key == SDLK_KP_PLUS;
                    const bool zoom_out = event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS;
                    if (zoom_in) {
                        host->zoom = std::min(2.0f, host->zoom * 1.125f);
                        zoom_shortcut = true;
                    } else if (zoom_out) {
                        host->zoom = std::max(0.5f, host->zoom / 1.125f);
                        zoom_shortcut = true;
                    }
                }
                if (!zoom_shortcut && host && host->session) {
                    ykm::InputViewport viewport{host->view.x, host->view.y, host->view.scale};
                    const ykm::SystemEvent system_event = system_event_from_sdl(event);
                    ykm::window_events_dispatch(*host->session, *host->system, viewport, system_event);
                }
            }
#ifdef YKM_DEBUG_CONSOLE
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F12 && !event.key.repeat) {
                debug_console.toggle();
            }
#endif
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }
        }
        #endif
        }
        if (!running) return;

        sessions.dispatch_messages();
        sessions.pump();
        if (!modal) sessions.start_pending();

        struct FrameTree {
            ykm::Session* session;
            JSValue tree;
        };
        std::vector<FrameTree> trees;
        std::vector<ykm::HostWindowRequest> requests;
        hosts.begin_frame();
        for (const auto& session : sessions.sessions()) {
            if (!session->has_render()) continue;
            JSValue tree = session->render();
            if (JS_IsException(tree)) {
                JS_FreeValue(session->context(), tree);
                continue;
            }
            ui.collect_windows(*session, tree, requests);
            trees.push_back({session.get(), tree});
        }
        const bool fallback = requests.empty() && !trees.empty();
        if (fallback) {
            ykm::HostWindowRequest host;
            host.session = trees.front().session;
            host.key = "host";
            host.title = "YKM Widgets";
            host.width = 960;
            host.height = 640;
            requests.push_back(host);
        }
        if (!modal) {
            for (const auto& request : requests) hosts.sync(request);
            hosts.drop_unseen();
        }

        bool debug_drawn = false;
        bool fonts_flushed = false;
        for (auto& host : hosts.list()) {
#ifdef _WIN32
            if (!ykm::HostWindows::sync_platform_surface(host)) continue;
#endif
            ImGui::SetCurrentContext(host.imgui);
            if (!fonts_flushed) {
                ykm::fonts_flush();
                fonts_flushed = true;
            }
#ifdef _WIN32
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
#else
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
#endif
            hosts.apply_canvas_io(host);
            const float layout_scale = (host.view.canvas_w > 0)
                ? static_cast<float>(host.view.pixel_w) / host.view.canvas_w * std::max(0.5f, host.zoom)
                : 1.0f;
            const bool zoom_changed = std::abs(host.zoom - host.last_zoom) > 0.001f;
            host.last_zoom = host.zoom;
            ykm::ui_zoom_sync(layout_scale, zoom_changed);
            ImGui::NewFrame();
            for (const auto& item : trees) {
                if (fallback) ui.render_tree(*item.session, item.tree, &host);
                else ui.render_named_window(*item.session, item.tree, host.key, &host);
            }
#ifdef YKM_DEBUG_CONSOLE
            if (!debug_drawn) {
                debug_console.render(sessions);
                debug_drawn = true;
            }
#endif
            ImGui::Render();
            hosts.present_canvas(host);
        }
        for (const auto& item : trees) {
            ui.finish(*item.session);
            JS_FreeValue(item.session->context(), item.tree);
        }
        if (!modal && hosts.list().empty() && !trees.empty()) running = false;
        if (modal) ImGui::SetCurrentContext(previous_context);
    };
#ifdef _WIN32
    ykm::system_set_frame_handler([](void* user) {
        (*static_cast<decltype(run_frame)*>(user))(true);
    }, &run_frame);
#endif
    while (running) {
#ifdef _WIN32
        ykm::system_pump_messages();
#endif
        run_frame(false);
    }
#ifdef _WIN32
    ykm::system_set_frame_handler(nullptr, nullptr);
#endif

    sessions.shutdown_workers();
    hosts.destroy_all();
#ifdef _WIN32
    ykm::d3d11_renderer().shutdown();
    ykm::system_set_message_handler(nullptr, nullptr);
#endif
#ifdef _WIN32
    CoUninitialize();
#endif
#ifndef _WIN32
    SDL_Quit();
#endif
    return 0;
}
