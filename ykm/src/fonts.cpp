#include "fonts.h"

#include "app_storage.h"
#include "js_util.h"
#include "session.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>
#ifndef _WIN32
#include <backends/imgui_impl_sdlrenderer3.h>
#endif
#ifdef _WIN32
#include "imgui_impl_dx11.h"
#endif

namespace ykm {
namespace {

struct PendingFont {
    std::string name;
    std::string path;
    float size = 16.0f;
    const uint8_t* data = nullptr;
    uint32_t data_size = 0;
};

std::vector<PendingFont> g_pending;
std::unordered_map<std::string, ImFont*> g_fonts;
bool g_rebuild = false;

struct ZoomState {
    ImGuiStyle base;
    float content = 1.0f;
    float output = 1.0f;
    int raster_px = 0;
    bool has_base = false;
};

std::unordered_map<ImGuiContext*, ZoomState> g_zoom;

ZoomState& zoom_state() {
    return g_zoom[ImGui::GetCurrentContext()];
}

constexpr float kMaxGlyphPx = 48.0f;

float zoom_font_size() {
    return std::clamp(18.0f * zoom_state().content, 16.0f, kMaxGlyphPx);
}

void rebuild_font_texture() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    io.Fonts->Build();
#ifdef _WIN32
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui_ImplDX11_CreateDeviceObjects();
#else
    ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
    ImGui_ImplSDLRenderer3_CreateDeviceObjects();
#endif
}

void reload_ui_font() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    g_fonts.erase("ui");
    fonts_load_ui_default();
    rebuild_font_texture();
}

JSValue js_fonts_add(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(context, "fonts.add({ name, file, size }) requires an options object");
    }
    if (!ImGui::GetCurrentContext()) {
        return JS_ThrowInternalError(context, "fonts require the GUI runtime");
    }
    JSValue name_value = JS_GetPropertyStr(context, argv[0], "name");
    JSValue file_value = JS_GetPropertyStr(context, argv[0], "file");
    JSValue size_value = JS_GetPropertyStr(context, argv[0], "size");
    const std::string name = js_to_string(context, name_value);
    const std::string file = js_to_string(context, file_value);
    double size = 16.0;
    if (!JS_IsUndefined(size_value)) JS_ToFloat64(context, &size, size_value);
    JS_FreeValue(context, name_value);
    JS_FreeValue(context, file_value);
    JS_FreeValue(context, size_value);
    if (name.empty() || file.empty()) {
        return JS_ThrowTypeError(context, "fonts.add requires name and file");
    }
    auto* session = Session::from_context(context);
    if (!session) return JS_ThrowInternalError(context, "session is unavailable");
    if (file.rfind("asset:/", 0) == 0) {
        const std::string asset_name = std::filesystem::path(file.substr(7)).lexically_normal().generic_string();
        AssetView asset;
        if (embedded_asset(asset_name, asset)) {
            g_pending.push_back({name, file, static_cast<float>(size), asset.data, asset.size});
            g_rebuild = true;
            return JS_TRUE;
        }
        const std::filesystem::path disk = session->root() / "assets" / asset_name;
        if (!fonts_add(name, disk.string(), static_cast<float>(size))) {
            return JS_ThrowReferenceError(context, "unable to queue font '%s'", name.c_str());
        }
        return JS_TRUE;
    }
    const std::filesystem::path root = std::filesystem::absolute(session->root()).lexically_normal();
    const std::filesystem::path input(file);
    const std::filesystem::path combined = (input.is_absolute() ? input : (root / input)).lexically_normal();
    std::string root_text = root.generic_string();
    std::string path_text = combined.generic_string();
#ifdef _WIN32
    for (char& c : root_text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : path_text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    if (path_text != root_text) {
        if (!root_text.empty() && root_text.back() != '/') root_text.push_back('/');
        if (path_text.size() < root_text.size() || path_text.compare(0, root_text.size(), root_text) != 0) {
            return JS_ThrowReferenceError(context, "font path escapes application root");
        }
    }
    if (!fonts_add(name, combined.string(), static_cast<float>(size))) {
        return JS_ThrowReferenceError(context, "unable to queue font '%s'", name.c_str());
    }
    return JS_TRUE;
}

JSValue js_fonts_list(JSContext* context, JSValueConst, int, JSValueConst*) {
    JSValue result = JS_NewArray(context);
    const auto names = fonts_list();
    for (uint32_t index = 0; index < names.size(); ++index) {
        JS_SetPropertyUint32(context, result, index, JS_NewString(context, names[index].c_str()));
    }
    return result;
}

} // namespace

bool fonts_add(const std::string& name, const std::string& path, float size) {
    g_pending.push_back({name, path, size, nullptr, 0});
    g_rebuild = true;
    return true;
}

ImFont* fonts_get(const std::string& name) {
    auto found = g_fonts.find(name);
    return found == g_fonts.end() ? nullptr : found->second;
}

std::vector<std::string> fonts_list() {
    std::vector<std::string> names;
    names.reserve(g_fonts.size());
    for (const auto& entry : g_fonts) names.push_back(entry.first);
    for (const auto& pending : g_pending) names.push_back(pending.name);
    return names;
}

void fonts_load_ui_default() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
        "C:\\Windows\\Fonts\\msyhl.ttc",
    };
    ImFontConfig config;
    config.FontNo = 0;
    config.PixelSnapH = true;
    config.OversampleH = 1;
    config.OversampleV = 1;
    for (const char* path : candidates) {
        if (!std::filesystem::exists(path)) continue;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            path, zoom_font_size(), &config, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (font) {
            g_fonts["ui"] = font;
            io.FontDefault = font;
            return;
        }
    }
}

void fonts_flush() {
    if (!g_rebuild || !ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    for (const PendingFont& font : g_pending) {
        ImFont* loaded = nullptr;
        if (font.data) {
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            loaded = io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(font.data),
                                                    static_cast<int>(font.data_size), font.size, &config);
        } else {
            loaded = io.Fonts->AddFontFromFileTTF(font.path.c_str(), font.size);
        }
        if (loaded) g_fonts[font.name] = loaded;
    }
    g_pending.clear();
    io.Fonts->Build();
    if (io.Fonts->TexID) {
#ifdef _WIN32
        ImGui_ImplDX11_InvalidateDeviceObjects();
#else
        ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
#endif
    }
    g_rebuild = false;
}

void ui_zoom_capture_base() {
    if (!ImGui::GetCurrentContext()) return;
    ZoomState& state = zoom_state();
    state.base = ImGui::GetStyle();
    state.has_base = true;
}

void ui_zoom_sync(float px_scale, bool rebuild_font) {
    if (!ImGui::GetCurrentContext()) return;
    ZoomState& state = zoom_state();
    px_scale = std::clamp(px_scale, 0.5f, 8.0f);
    px_scale = std::round(px_scale * 32.0f) / 32.0f;
    const float needed = 18.0f * px_scale;
    const int raster = static_cast<int>(std::lround(std::clamp(needed, 16.0f, kMaxGlyphPx)));
    const bool style_changed = !state.has_base || std::abs(state.content - px_scale) > 0.02f;
    const bool font_changed = raster != state.raster_px;
    if (!style_changed && !font_changed && !rebuild_font) return;
    if (!state.has_base) ui_zoom_capture_base();
    state.content = px_scale;
    state.output = px_scale;
    ImGui::GetStyle() = state.base;
    ImGui::GetStyle().ScaleAllSizes(px_scale);
    if (state.raster_px == 0) rebuild_font = true;
    if (rebuild_font && (font_changed || state.raster_px != raster)) {
        state.raster_px = raster;
        reload_ui_font();
    }
    if (ImFont* font = ImGui::GetIO().FontDefault) {
        font->Scale = needed / std::max(1.0f, font->FontSize);
    }
}

void fonts_install_js(JSContext* context, JSValue api) {
    JSValue fonts = JS_NewObject(context);
    JS_SetPropertyStr(context, fonts, "add", JS_NewCFunction(context, js_fonts_add, "add", 1));
    JS_SetPropertyStr(context, fonts, "list", JS_NewCFunction(context, js_fonts_list, "list", 0));
    JS_SetPropertyStr(context, api, "fonts", fonts);
}

} // namespace ykm
