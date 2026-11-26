#pragma once

#include "system_window.h"
#ifdef _WIN32

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace ykm {

struct D3D11WindowResources {
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;
    int width = 0;
    int height = 0;
    HWND bound_window = nullptr;
    uint64_t surface_generation = 0;

    void detach(ID3D11DeviceContext* context);
    bool sync_surface(ID3D11Device* device, ID3D11DeviceContext* context,
                      NativeSurface surface, int width, int height);

    void release();
    bool create(ID3D11Device* device, HWND hwnd, int width, int height);
    bool resize(ID3D11Device* device, int width, int height);
};

class D3D11Renderer {
public:
    bool initialize();
    void shutdown();

    bool initialized() const { return device_ != nullptr && context_ != nullptr; }
    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }

private:
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
};

D3D11Renderer& d3d11_renderer();

} // namespace ykm

#endif
