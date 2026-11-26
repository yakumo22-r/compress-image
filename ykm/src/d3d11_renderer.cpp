#include "d3d11_renderer.h"

#ifdef _WIN32

#include <algorithm>

namespace ykm {

void D3D11WindowResources::detach(ID3D11DeviceContext* context) {
    if (context && swap_chain) context->OMSetRenderTargets(0, nullptr, nullptr);
    release();
}

bool D3D11WindowResources::sync_surface(ID3D11Device* device, ID3D11DeviceContext* context,
                                       NativeSurface surface, int new_width, int new_height) {
    if (!device || !context || !surface || surface.kind != SurfaceKind::Win32Window) {
        detach(context);
        return false;
    }
    HWND hwnd = static_cast<HWND>(surface.handle);
    if (bound_window != hwnd || surface_generation != surface.generation) detach(context);
    if (new_width <= 0 || new_height <= 0) return false;
    if (!swap_chain) {
        if (!create(device, hwnd, new_width, new_height)) return false;
        bound_window = hwnd;
        surface_generation = surface.generation;
    } else if (width != new_width || height != new_height) {
        if (!resize(device, new_width, new_height)) return false;
    }
    return render_target != nullptr;
}

void D3D11WindowResources::release() {
    if (render_target) { render_target->Release(); render_target = nullptr; }
    if (swap_chain) { swap_chain->Release(); swap_chain = nullptr; }
    width = 0;
    height = 0;
    bound_window = nullptr;
    surface_generation = 0;
}

bool D3D11WindowResources::create(ID3D11Device* device, HWND hwnd, int new_width, int new_height) {
    release();
    IDXGIDevice* dxgi_device = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;
    bool ok = false;
    do {
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device)))) break;
        if (FAILED(dxgi_device->GetAdapter(&adapter))) break;
        if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) break;
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = static_cast<UINT>(std::max(1, new_width));
        desc.BufferDesc.Height = static_cast<UINT>(std::max(1, new_height));
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hwnd;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        if (FAILED(factory->CreateSwapChain(device, &desc, &swap_chain))) break;
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        width = std::max(1, new_width);
        height = std::max(1, new_height);
        ID3D11Texture2D* back_buffer = nullptr;
        if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) break;
        const HRESULT target_result = device->CreateRenderTargetView(back_buffer, nullptr, &render_target);
        back_buffer->Release();
        if (FAILED(target_result)) break;
        ok = true;
    } while (false);
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgi_device) dxgi_device->Release();
    if (!ok) release();
    return ok;
}

bool D3D11WindowResources::resize(ID3D11Device* device, int new_width, int new_height) {
    if (!swap_chain) return false;
    // The immediate context retains bound RTVs; ResizeBuffers requires all references released.
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    context->Release();
    if (render_target) { render_target->Release(); render_target = nullptr; }
    const UINT width_value = static_cast<UINT>(std::max(1, new_width));
    const UINT height_value = static_cast<UINT>(std::max(1, new_height));
    if (FAILED(swap_chain->ResizeBuffers(0, width_value, height_value, DXGI_FORMAT_UNKNOWN, 0))) {
        release();
        return false;
    }
    ID3D11Texture2D* back_buffer = nullptr;
    if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
        release();
        return false;
    }
    const HRESULT result = device->CreateRenderTargetView(back_buffer, nullptr, &render_target);
    back_buffer->Release();
    if (FAILED(result)) {
        release();
        return false;
    }
    width = static_cast<int>(width_value);
    height = static_cast<int>(height_value);
    return true;
}

bool D3D11Renderer::initialize() {
    if (initialized()) return true;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL selected{};
    const HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                              levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                              &device_, &selected, &context_);
    if (FAILED(result)) {
        device_ = nullptr;
        context_ = nullptr;
        return false;
    }
    return true;
}

void D3D11Renderer::shutdown() {
    if (context_) { context_->Release(); context_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

D3D11Renderer& d3d11_renderer() {
    static D3D11Renderer renderer;
    return renderer;
}

} // namespace ykm

#endif
