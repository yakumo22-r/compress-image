#include "host_windows.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "session.h"

#include <imgui.h>
#include <iostream>
#include <stdexcept>

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void draw(ykm::HostWindows& hosts, ykm::HostWindow& host) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    hosts.apply_canvas_io(host);
    ImGui::NewFrame();
    ImGui::Begin("Surface probe");
    ImGui::TextUnformatted("Surface probe");
    ImGui::End();
    ImGui::Render();
    hosts.present_canvas(host);
    check(host.d3d.swap_chain && host.d3d.render_target &&
          host.d3d.bound_window == host.system->surface().handle, "presentation target not bound");
}

static void run() {
    ykm::SessionManager sessions;
    ykm::Session session(sessions, "surface-probe");
    auto* js = session.context();
    check(js != nullptr, "Session initialization failed");
    ykm::HostWindows hosts;
    ykm::HostWindowRequest request;
    request.session = &session;
    request.key = "surface-probe";
    request.title = "YKM host surface regression";
    auto* host = hosts.sync(request);
    check(host != nullptr, "initial host attach failed");
    auto* imgui = host->imgui;
    const auto old = host->system->surface();
    const auto id = host->system->id();
    ImGui::SetCurrentContext(imgui);
    ImGui::GetIO().IniFilename = nullptr;
    auto* renderer = ImGui::GetIO().BackendRendererUserData;
    draw(hosts, *host);

    // Create another host so retirement cannot rely on a stored vector element pointer.
    auto sibling = request;
    sibling.key = "sibling";
    auto* other = hosts.sync(sibling);
    check(other != nullptr, "sibling creation failed");
    auto* other_imgui = other->imgui;
    ImGui::SetCurrentContext(other_imgui);
    ImGui::GetIO().IniFilename = nullptr;
    host = hosts.find(id);
    check(host != nullptr, "primary host lookup failed");
    check(DestroyWindow(static_cast<HWND>(old.handle)) != FALSE, "old HWND destruction failed");
    check(ImGui::GetCurrentContext() == other_imgui, "retirement did not restore the previous context");
    check(!host->system->surface() && !host->imgui_surface && !host->d3d.swap_chain && !host->d3d.render_target,
          "retirement left native bindings alive");
    check(!ykm::HostWindows::sync_platform_surface(*host), "missing surface should not permit a frame");
    check(hosts.sync(request) == host, "retirement replaced the logical host");
    ImGui::SetCurrentContext(imgui);
    check(!ImGui::GetIO().BackendPlatformUserData && !ImGui::GetMainViewport()->PlatformHandle &&
          !ImGui::GetMainViewport()->PlatformHandleRaw, "retirement left stale ImGui platform handles");
    check(ImGui::GetIO().BackendRendererUserData == renderer, "retirement replaced the renderer backend");
    const int frame = ImGui::GetFrameCount();

    // Supply a real replacement HWND at the test boundary. Production Win32
    // does not yet republish a replacement on an existing SystemWindow.
    ykm::SystemWindowOptions replacement;
    replacement.title = request.title;
    auto next = ykm::create_system_window(replacement);
    check(next != nullptr, "replacement HWND creation failed");
    host->system = std::move(next);
    ImGui::SetCurrentContext(other_imgui);
    check(ykm::HostWindows::sync_platform_surface(*host), "replacement attach failed");
    check(ImGui::GetCurrentContext() == other_imgui, "attach did not restore the previous context");
    ImGui::SetCurrentContext(imgui);
    check(host->imgui == imgui && host->session == &session && session.context() == js &&
          ImGui::GetIO().BackendRendererUserData == renderer, "replacement reset retained state");
    check(host->imgui_surface == host->system->surface() &&
          ImGui::GetMainViewport()->PlatformHandleRaw == host->imgui_surface.handle,
          "ImGui did not bind the replacement HWND");
    const auto* platform = ImGui::GetIO().BackendPlatformUserData;
    check(ykm::HostWindows::sync_platform_surface(*host) && ImGui::GetIO().BackendPlatformUserData == platform,
          "unchanged surface reinitialized the backend");
    draw(hosts, *host);
    check(ImGui::GetFrameCount() == frame + 1, "replacement did not resume the same context's frames");
}

int main() {
    int result = 0;
    try {
        run();
        std::cout << "Win32 retirement and ImGui rebind: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    ykm::d3d11_renderer().shutdown();
    return result;
}
