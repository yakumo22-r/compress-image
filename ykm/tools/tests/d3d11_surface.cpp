#include "d3d11_renderer.h"
#include <iostream>
#include <stdexcept>

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    ykm::D3D11WindowResources resources;
    ykm::D3D11Renderer renderer;
    HWND first = nullptr, second = nullptr;
    try {
        first = CreateWindowExW(0, L"STATIC", L"Surface test one", WS_OVERLAPPEDWINDOW,
                                0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        second = CreateWindowExW(0, L"STATIC", L"Surface test two", WS_OVERLAPPEDWINDOW,
                                 0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(first && second, "test window creation failed");
        check(renderer.initialize(), "D3D11 initialization failed");
        auto sync = [&](ykm::NativeSurface surface, int w = 320, int h = 240) {
            return resources.sync_surface(renderer.device(), renderer.context(), surface, w, h);
        };
        ykm::NativeSurface surface{ykm::SurfaceKind::Win32Window, first, 1};
        check(sync(surface), "initial attach failed");
        auto* chain = resources.swap_chain;
        check(sync(surface) && resources.swap_chain == chain, "unchanged surface not reused");
        check(!sync(surface, 0, 0) && resources.swap_chain == chain, "zero size should pause presentation");
        check(sync(surface, 360, 260) && resources.width == 360, "resize failed");
        renderer.context()->OMSetRenderTargets(1, &resources.render_target, nullptr);
        resources.detach(renderer.context());
        ID3D11RenderTargetView* bound = nullptr;
        renderer.context()->OMGetRenderTargets(1, &bound, nullptr);
        const bool unbound = bound == nullptr;
        if (bound) bound->Release();
        check(unbound && !resources.swap_chain && !resources.render_target &&
              !resources.bound_window && resources.surface_generation == 0, "detach retained resources");
        check(sync(surface), "reattach failed");
        surface.generation = 2;
        check(sync(surface) && resources.surface_generation == 2, "same-handle generation change failed");
        surface.handle = second;
        check(sync(surface) && resources.bound_window == second, "second-window attach failed");
        DXGI_SWAP_CHAIN_DESC desc{};
        check(SUCCEEDED(resources.swap_chain->GetDesc(&desc)) && desc.OutputWindow == second,
              "swapchain still targets old window");
        check(!sync({}) && !resources.swap_chain, "absent surface did not detach");
        check(sync(surface), "attach after surface loss failed");
        check(!sync({ykm::SurfaceKind::SdlWindow, second, 3}) && !resources.swap_chain,
              "incompatible surface not rejected/detached");
        std::cout << "D3D11 surface reuse, resize, detach, generation and reattach: OK\n";
        resources.detach(renderer.context());
        renderer.shutdown();
        DestroyWindow(second);
        DestroyWindow(first);
        return 0;
    } catch (const std::exception& error) {
        resources.detach(renderer.context());
        renderer.shutdown();
        if (second) DestroyWindow(second);
        if (first) DestroyWindow(first);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
