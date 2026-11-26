#include "debug_log.h"
#include "ui_renderer.h"

#include "window_layer.h"
#include "component_relations.h"
#include "fonts.h"
#include "session.h"
#include "app_storage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#ifndef _WIN32
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#endif
#ifdef _WIN32
#include <shellapi.h>
#include <wincodec.h>
#include "d3d11_renderer.h"
#include "imgui_impl_dx11.h"
#endif

namespace ykm {
namespace {

constexpr int kMaxNodes = 4096;
constexpr int kMaxDepth = 64;
float g_layout_scale = 1.0f;

float canvas_px(float value) { return value * g_layout_scale; }

std::string string_property(JSContext* context, JSValueConst object, const char* name, const std::string& fallback = {}) {
    JSValue value = JS_GetPropertyStr(context, object, name);
    if (!JS_IsString(value)) {
        JS_FreeValue(context, value);
        return fallback;
    }
    const char* text = JS_ToCString(context, value);
    std::string result = text ? text : fallback;
    if (text) {
        JS_FreeCString(context, text);
    }
    JS_FreeValue(context, value);
    return result;
}

bool bool_property(JSContext* context, JSValueConst object, const char* name, bool fallback) {
    JSValue value = JS_GetPropertyStr(context, object, name);
    if (JS_IsUndefined(value)) {
        JS_FreeValue(context, value);
        return fallback;
    }
    const int result = JS_ToBool(context, value);
    JS_FreeValue(context, value);
    return result >= 0 ? result != 0 : fallback;
}

double number_property(JSContext* context, JSValueConst object, const char* name, double fallback) {
    JSValue value = JS_GetPropertyStr(context, object, name);
    double result = fallback;
    if (JS_ToFloat64(context, &result, value) < 0 || !std::isfinite(result)) {
        result = fallback;
    }
    JS_FreeValue(context, value);
    return result;
}

bool has_property(JSContext* context, JSValueConst object, const char* name) {
    JSValue value = JS_GetPropertyStr(context, object, name);
    const bool result = !JS_IsUndefined(value);
    JS_FreeValue(context, value);
    return result;
}

bool hex_color(const std::string& text, float opacity, ImU32& color) {
    if (text.empty() || text[0] != '#') return false;
    const size_t digits = text.size() - 1;
    if (digits != 3 && digits != 4 && digits != 6 && digits != 8) return false;
    auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
        return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
    };
    int components[4] = {0, 0, 0, 255};
    const int count = (digits == 3 || digits == 4) ? static_cast<int>(digits) : static_cast<int>(digits / 2);
    for (int index = 0; index < count; ++index) {
        const int high = nibble(text[1 + index * (digits <= 4 ? 1 : 2)]);
        if (high < 0) return false;
        if (digits <= 4) components[index] = high * 17;
        else {
            const int low = nibble(text[2 + index * 2]);
            if (low < 0) return false;
            components[index] = high * 16 + low;
        }
    }
    components[3] = static_cast<int>(components[3] * std::clamp(opacity, 0.0f, 1.0f));
    color = IM_COL32(components[0], components[1], components[2], components[3]);
    return true;
}

bool color_property(JSContext* context, JSValueConst object, const char* name, float opacity, ImU32& color) {
    JSValue value = JS_GetPropertyStr(context, object, name);
    if (!JS_IsString(value)) {
        JS_FreeValue(context, value);
        return false;
    }
    const char* text = JS_ToCString(context, value);
    const bool valid = text && hex_color(text, opacity, color);
    if (text) JS_FreeCString(context, text);
    JS_FreeValue(context, value);
    return valid;
}

// Scope these overrides to a native Window's component tree.
struct ControlStyleScope {
    int colors = 0;
    int vars = 0;

    ControlStyleScope(JSContext* js, JSValueConst props) {
        JSValue style = JS_GetPropertyStr(js, props, "controlStyle");
        if (JS_IsObject(style)) {
            const auto push_color = [&](const char* key, ImGuiCol slot) {
                ImU32 color;
                if (color_property(js, style, key, 1.0f, color)) {
                    ImGui::PushStyleColor(slot, color);
                    ++colors;
                }
            };
            const auto push_size = [&](const char* key, ImGuiStyleVar slot) {
                const double value = number_property(js, style, key, -1);
                if (value >= 0) {
                    ImGui::PushStyleVar(slot, canvas_px(static_cast<float>(std::min(value, 32.0))));
                    ++vars;
                }
            };
            push_color("text", ImGuiCol_Text);
            push_color("frame", ImGuiCol_FrameBg);
            push_color("frameHovered", ImGuiCol_FrameBgHovered);
            push_color("frameActive", ImGuiCol_FrameBgActive);
            push_color("button", ImGuiCol_Button);
            push_color("buttonHovered", ImGuiCol_ButtonHovered);
            push_color("buttonActive", ImGuiCol_ButtonActive);
            push_color("border", ImGuiCol_Border);
            push_color("accent", ImGuiCol_CheckMark);
            push_color("accent", ImGuiCol_SliderGrab);
            push_color("accent", ImGuiCol_SliderGrabActive);
            push_color("accent", ImGuiCol_PlotHistogram);
            push_size("frameRadius", ImGuiStyleVar_FrameRounding);
            push_size("grabRadius", ImGuiStyleVar_GrabRounding);
            push_size("borderWidth", ImGuiStyleVar_FrameBorderSize);
            const double padding_y = number_property(js, style, "framePaddingY", -1);
            if (padding_y >= 0) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x,
                    canvas_px(static_cast<float>(std::min(padding_y, 32.0)))));
                ++vars;
            }
        }
        JS_FreeValue(js, style);
    }

    ~ControlStyleScope() {
        if (vars) ImGui::PopStyleVar(vars);
        if (colors) ImGui::PopStyleColor(colors);
    }
};

void draw_vector_commands(JSContext* context, JSValueConst drawing, const ImVec2& origin) {
    JSValue commands = JS_DupValue(context, drawing);
    if (!JS_IsArray(commands) && JS_IsObject(commands)) {
        JS_FreeValue(context, commands);
        commands = JS_GetPropertyStr(context, drawing, "commands");
    }
    if (!JS_IsArray(commands)) {
        JS_FreeValue(context, commands);
        return;
    }
    uint32_t length = 0;
    JSValue length_value = JS_GetPropertyStr(context, commands, "length");
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (uint32_t index = 0; index < length; ++index) {
        JSValue command = JS_GetPropertyUint32(context, commands, index);
        if (!JS_IsObject(command)) {
            JS_FreeValue(context, command);
            continue;
        }
        const std::string kind = string_property(context, command, "kind");
        const float opacity = static_cast<float>(number_property(context, command, "opacity", 1.0));
        ImU32 fill = 0;
        ImU32 stroke = 0;
        const bool has_fill = color_property(context, command, "fill", opacity, fill);
        const bool has_stroke = color_property(context, command, "stroke", opacity, stroke);
        const float stroke_width = canvas_px(std::max(0.0f, static_cast<float>(
            number_property(context, command, "strokeWidth", 1.0))));
        if (kind == "rect" || kind == "roundedRect") {
            const float x = origin.x + canvas_px(static_cast<float>(number_property(context, command, "x", 0)));
            const float y = origin.y + canvas_px(static_cast<float>(number_property(context, command, "y", 0)));
            const float width = canvas_px(static_cast<float>(number_property(context, command, "width", 0)));
            const float height = canvas_px(static_cast<float>(number_property(context, command, "height", 0)));
            const float radius = kind == "roundedRect"
                ? canvas_px(std::max(0.0f, static_cast<float>(number_property(context, command, "radius", 0))))
                : 0.0f;
            const ImVec2 min(x, y);
            const ImVec2 max(x + width, y + height);
            if (has_fill) draw->AddRectFilled(min, max, fill, radius);
            if (has_stroke && stroke_width > 0) draw->AddRect(min, max, stroke, radius, 0, stroke_width);
        } else if (kind == "circle") {
            const ImVec2 center(
                origin.x + canvas_px(static_cast<float>(number_property(context, command, "x", 0))),
                origin.y + canvas_px(static_cast<float>(number_property(context, command, "y", 0))));
            const float radius = canvas_px(std::max(0.0f, static_cast<float>(number_property(context, command, "radius", 0))));
            if (has_fill) draw->AddCircleFilled(center, radius, fill);
            if (has_stroke && stroke_width > 0) draw->AddCircle(center, radius, stroke, 0, stroke_width);
        } else if (kind == "ellipse") {
            const ImVec2 center(
                origin.x + canvas_px(static_cast<float>(number_property(context, command, "x", 0))),
                origin.y + canvas_px(static_cast<float>(number_property(context, command, "y", 0))));
            const ImVec2 radius(
                canvas_px(std::max(0.0f, static_cast<float>(number_property(context, command, "radiusX", 0)))),
                canvas_px(std::max(0.0f, static_cast<float>(number_property(context, command, "radiusY", 0)))));
            if (has_fill) draw->AddEllipseFilled(center, radius, fill);
            if (has_stroke && stroke_width > 0) draw->AddEllipse(center, radius, stroke, 0, stroke_width);
        } else if (kind == "line" && has_stroke && stroke_width > 0) {
            const ImVec2 from(
                origin.x + canvas_px(static_cast<float>(number_property(context, command, "x1", 0))),
                origin.y + canvas_px(static_cast<float>(number_property(context, command, "y1", 0))));
            const ImVec2 to(
                origin.x + canvas_px(static_cast<float>(number_property(context, command, "x2", 0))),
                origin.y + canvas_px(static_cast<float>(number_property(context, command, "y2", 0))));
            draw->AddLine(from, to, stroke, stroke_width);
        }
        JS_FreeValue(context, command);
    }
    JS_FreeValue(context, commands);
}

void draw_control_background(JSContext* context, JSValueConst props, const ImVec2& min, const ImVec2& size) {
    JSValue background = JS_GetPropertyStr(context, props, "background");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    if (JS_IsString(background)) {
        const char* text = JS_ToCString(context, background);
        ImU32 fill = 0;
        if (text && hex_color(text, 1.0f, fill)) draw->AddRectFilled(min, max, fill);
        if (text) JS_FreeCString(context, text);
    } else if (JS_IsObject(background)) {
        const float opacity = static_cast<float>(number_property(context, background, "opacity", 1.0));
        const float radius = canvas_px(std::max(0.0f, static_cast<float>(
            number_property(context, background, "radius", 0))));
        const float stroke_width = canvas_px(std::max(0.0f, static_cast<float>(
            number_property(context, background, "strokeWidth", 1))));
        ImU32 color = 0;
        if (color_property(context, background, "fill", opacity, color)) draw->AddRectFilled(min, max, color, radius);
        if (stroke_width > 0 && color_property(context, background, "stroke", opacity, color)) {
            draw->AddRect(min, max, color, radius, 0, stroke_width);
        }
        JSValue drawing = JS_GetPropertyStr(context, background, "drawing");
        draw->PushClipRect(min, max, true);
        draw_vector_commands(context, drawing, min);
        draw->PopClipRect();
        JS_FreeValue(context, drawing);
    }
    JS_FreeValue(context, background);
}

void begin_host_client(bool menu) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
    (void)menu;
    ImGui::Begin("##ykm-client", nullptr, flags);
    ImGui::PopStyleVar(3);
}

bool overflow_scroll(JSContext* context, JSValueConst props) {
    const std::string value = string_property(context, props, "overflow");
    return value == "scroll" || value == "auto";
}

ImGuiWindowFlags overflow_window_flags(JSContext* context, JSValueConst props) {
    ImGuiWindowFlags flags = 0;
    const auto overflow = string_property(context, props, "overflow");
    if (overflow == "hidden") return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (overflow == "scroll") {
        flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
    }
    return flags;
}

bool begin_overflow(JSContext* context, JSValueConst props, const std::string& key, float width, float height) {
    if (!overflow_scroll(context, props)) return false;
    float child_w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    float child_h = height > 0 ? height : ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild((key + "/overflow").c_str(), ImVec2(child_w, child_h), ImGuiChildFlags_None,
                      overflow_window_flags(context, props));
    return true;
}

struct SelectOption {
    std::string value;
    std::string label;
};

uint32_t array_length(JSContext* context, JSValueConst array) {
    uint32_t length = 0;
    JSValue length_value = JS_GetPropertyStr(context, array, "length");
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);
    return length;
}

std::vector<SelectOption> read_options(JSContext* context, JSValueConst props) {
    std::vector<SelectOption> result;
    JSValue options = JS_GetPropertyStr(context, props, "options");
    if (!JS_IsArray(options)) {
        JS_FreeValue(context, options);
        options = JS_GetPropertyStr(context, props, "items");
    }
    if (!JS_IsArray(options)) {
        JS_FreeValue(context, options);
        return result;
    }
    const uint32_t length = std::min<uint32_t>(array_length(context, options), 256);
    for (uint32_t index = 0; index < length; ++index) {
        JSValue item = JS_GetPropertyUint32(context, options, index);
        SelectOption option;
        if (JS_IsString(item)) {
            const char* text = JS_ToCString(context, item);
            option.value = text ? text : "";
            option.label = option.value;
            if (text) JS_FreeCString(context, text);
        } else if (JS_IsObject(item)) {
            option.value = string_property(context, item, "value");
            option.label = string_property(context, item, "label", option.value);
            if (option.value.empty()) option.value = option.label;
        }
        if (!option.value.empty() || !option.label.empty()) result.push_back(std::move(option));
        JS_FreeValue(context, item);
    }
    JS_FreeValue(context, options);
    return result;
}

std::vector<std::string> read_values(JSContext* context, JSValueConst props) {
    std::vector<std::string> result;
    JSValue value = JS_GetPropertyStr(context, props, "value");
    if (JS_IsArray(value)) {
        const uint32_t length = std::min<uint32_t>(array_length(context, value), 256);
        for (uint32_t index = 0; index < length; ++index) {
            JSValue item = JS_GetPropertyUint32(context, value, index);
            if (JS_IsString(item)) {
                const char* text = JS_ToCString(context, item);
                if (text) {
                    result.emplace_back(text);
                    JS_FreeCString(context, text);
                }
            }
            JS_FreeValue(context, item);
        }
    } else if (JS_IsString(value)) {
        const char* text = JS_ToCString(context, value);
        if (text && *text) result.emplace_back(text);
        if (text) JS_FreeCString(context, text);
    }
    JS_FreeValue(context, value);
    return result;
}

JSValue callback_property(JSContext* context, JSValueConst object, const char* name) {
    JSValue value = JS_GetPropertyStr(context, object, name);
    if (!JS_IsFunction(context, value)) {
        JS_FreeValue(context, value);
        return JS_UNDEFINED;
    }
    return value;
}

JSValue unwrap_tree(JSContext* context, JSValueConst tree) {
    if (!JS_IsObject(tree)) return JS_DupValue(context, tree);
    JSValue version = JS_GetPropertyStr(context, tree, "__ykmTreeVersion");
    const bool is_tree = !JS_IsUndefined(version) && !JS_IsNull(version);
    JS_FreeValue(context, version);
    if (!is_tree) return JS_DupValue(context, tree);
    return JS_GetPropertyStr(context, tree, "root");
}

} // namespace

struct UiRenderer::RenderControlContext {
    Session& session;
    JSContext* js;
    JSValueConst props;
    JSValueConst children;
    int depth;
    const std::string& type;
    const std::string& key;
    const std::string& scoped_key;
    float width;
    float height;
    bool has_position;
    ImVec2 position;
    ImVec2 start;
};

UiRenderer::ControlRenderer UiRenderer::control_renderer(const std::string& type) {
    static const std::unordered_map<std::string, ControlRenderer> renderers = {
        {"window", &UiRenderer::render_window_control},
        {"column", &UiRenderer::render_column_control},
        {"page", &UiRenderer::render_page_control},
        {"row", &UiRenderer::render_row_control},
        {"group", &UiRenderer::render_container_control},
        {"panel", &UiRenderer::render_container_control},
        {"modal", &UiRenderer::render_modal_control},
        {"root", &UiRenderer::render_container_control},
        {"graphics", &UiRenderer::render_graphics_control},
        {"inspect", &UiRenderer::render_container_control},
        {"jinspect", &UiRenderer::render_container_control},
        {"xml-inspect", &UiRenderer::render_container_control},
        {"yaml-inspect", &UiRenderer::render_container_control},
        {"toml-inspect", &UiRenderer::render_container_control},
        {"text", &UiRenderer::render_text_control},
        {"treeIndent", &UiRenderer::render_tree_indent_control},
        {"typeBadge", &UiRenderer::render_type_badge_control},
        {"disclosure", &UiRenderer::render_disclosure_control},
        {"iconButton", &UiRenderer::render_icon_button_control},
        {"button", &UiRenderer::render_button_control},
        {"checkbox", &UiRenderer::render_checkbox_control},
        {"input", &UiRenderer::render_input_control},
        {"slider", &UiRenderer::render_slider_control},
        {"separator", &UiRenderer::render_separator_control},
        {"select", &UiRenderer::render_select_control},
        {"radio", &UiRenderer::render_radio_control},
        {"tabs", &UiRenderer::render_tabs_control},
        {"list", &UiRenderer::render_list_control},
        {"scroll", &UiRenderer::render_scroll_control},
        {"progress", &UiRenderer::render_progress_control},
        {"textArea", &UiRenderer::render_text_area_control},
        {"numberInput", &UiRenderer::render_number_input_control},
        {"image", &UiRenderer::render_image_control},
        {"tree", &UiRenderer::render_tree_control},
    };
    const auto renderer = renderers.find(type);
    return renderer == renderers.end() ? nullptr : renderer->second;
}

void UiRenderer::set_app_metadata(std::string package_name, std::string version, std::string repository) {
    package_name_ = std::move(package_name);
    version_ = std::move(version);
    repository_ = std::move(repository);
}

void UiRenderer::collect_windows(Session& session, JSValueConst tree, std::vector<HostWindowRequest>& out) {
    JSValue root = unwrap_tree(session.context(), tree);
    collect_node(session, root, out);
    JS_FreeValue(session.context(), root);
}

void UiRenderer::render_named_window(Session& session, JSValueConst tree, const std::string& key, HostWindow* host) {
    current_host_ = host;
    g_layout_scale = 1.0f;
    if (host && host->view.canvas_w > 0) {
        g_layout_scale = static_cast<float>(host->view.pixel_w) / host->view.canvas_w * std::max(0.5f, host->zoom);
    }
    node_count_ = 0;
    skip_window_nodes_ = true;
    JSValue root = unwrap_tree(session.context(), tree);
    render_named_node(session, root, key);
    JS_FreeValue(session.context(), root);
    skip_window_nodes_ = false;
    current_host_ = nullptr;
}

void UiRenderer::render_tree(Session& session, JSValueConst tree, HostWindow* host) {
    current_host_ = host;
    node_count_ = 0;
    skip_window_nodes_ = false;
    begin_host_client(false);
    JSValue root = unwrap_tree(session.context(), tree);
    render_node(session, root, 0, session.id());
    JS_FreeValue(session.context(), root);
    ImGui::End();
    current_host_ = nullptr;
}

void UiRenderer::finish(Session& session) {
    dispatch_events(session);
}

void UiRenderer::collect_node(Session& session, JSValueConst node, std::vector<HostWindowRequest>& out) {
    JSContext* context = session.context();
    if (!JS_IsObject(node)) return;
    JSValue type_value = JS_GetPropertyStr(context, node, "type");
    const char* type_text = JS_ToCString(context, type_value);
    const std::string type = type_text ? type_text : "";
    if (type_text) JS_FreeCString(context, type_text);
    JS_FreeValue(context, type_value);
    JSValue props = JS_GetPropertyStr(context, node, "props");
    JSValue children = JS_GetPropertyStr(context, node, "children");
    if (type == "window") {
        HostWindowRequest request;
        request.session = &session;
        request.key = string_property(context, props, "key", session.id());
        request.title = string_property(context, props, "title", request.key);
        request.width = static_cast<float>(number_property(context, props, "width", 480));
        request.height = static_cast<float>(number_property(context, props, "height", 320));
        request.min_width = static_cast<int>(std::clamp(number_property(context, props, "minWidth", 0), 0.0, 8192.0));
        request.min_height = static_cast<int>(std::clamp(number_property(context, props, "minHeight", 0), 0.0, 8192.0));
        request.has_position = has_property(context, props, "x") || has_property(context, props, "y");
        request.x = static_cast<float>(number_property(context, props, "x", 0));
        request.y = static_cast<float>(number_property(context, props, "y", 0));
        const int32_t layer = static_cast<int32_t>(number_property(
            context, props, "layer", static_cast<int32_t>(WindowLayer::Page)));
        request.layer = (layer >= static_cast<int32_t>(WindowLayer::BG) &&
                         layer <= static_cast<int32_t>(WindowLayer::Debug))
            ? static_cast<WindowLayer>(layer)
            : WindowLayer::Page;
        out.push_back(request);
    }
    collect_node(session, children, out);
    if (JS_IsArray(children)) {
        uint32_t length = 0;
        JSValue length_value = JS_GetPropertyStr(context, children, "length");
        JS_ToUint32(context, &length, length_value);
        JS_FreeValue(context, length_value);
        for (uint32_t index = 0; index < length; ++index) {
            JSValue child = JS_GetPropertyUint32(context, children, index);
            collect_node(session, child, out);
            JS_FreeValue(context, child);
        }
    }
    JS_FreeValue(context, children);
    JS_FreeValue(context, props);
}

bool UiRenderer::render_named_node(Session& session, JSValueConst node, const std::string& key) {
    JSContext* context = session.context();
    if (!JS_IsObject(node)) return false;
    JSValue type_value = JS_GetPropertyStr(context, node, "type");
    const char* type_text = JS_ToCString(context, type_value);
    const std::string type = type_text ? type_text : "";
    if (type_text) JS_FreeCString(context, type_text);
    JS_FreeValue(context, type_value);
    JSValue props = JS_GetPropertyStr(context, node, "props");
    JSValue children = JS_GetPropertyStr(context, node, "children");
    bool found = false;
    if (type == "window" && string_property(context, props, "key", session.id()) == key) {
        render_window_contents(session, node, key);
        found = true;
    } else {
        found = render_named_node(session, children, key);
        if (!found && JS_IsArray(children)) {
            uint32_t length = 0;
            JSValue length_value = JS_GetPropertyStr(context, children, "length");
            JS_ToUint32(context, &length, length_value);
            JS_FreeValue(context, length_value);
            for (uint32_t index = 0; index < length && !found; ++index) {
                JSValue child = JS_GetPropertyUint32(context, children, index);
                found = render_named_node(session, child, key);
                JS_FreeValue(context, child);
            }
        }
    }
    JS_FreeValue(context, children);
    JS_FreeValue(context, props);
    return found;
}

void UiRenderer::render_window_contents(Session& session, JSValueConst node, const std::string& path) {
    JSContext* context = session.context();
    JSValue props = JS_GetPropertyStr(context, node, "props");
    JSValue children = JS_GetPropertyStr(context, node, "children");
    const float padding = canvas_px(static_cast<float>(number_property(context, props, "padding", 0)));
    JSValue menu = JS_GetPropertyStr(context, props, "menu");
    const bool has_menu = JS_IsArray(menu) || bool_property(context, props, "menu", false);
    begin_host_client(has_menu);
#ifdef _WIN32
    const auto background = string_property(context, props, "backgroundImage");
    if (background.rfind("asset:/", 0) == 0 && app_allows("fs.asset")) {
        if (auto* raw = load_win32_image(background)) {
            auto* texture = static_cast<ID3D11ShaderResourceView*>(raw);
            ID3D11Resource* resource = nullptr;
            ID3D11Texture2D* image = nullptr;
            texture->GetResource(&resource);
            if (SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&image)))) {
                D3D11_TEXTURE2D_DESC desc{};
                image->GetDesc(&desc);
                const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
                if (size.x > 0 && size.y > 0 && desc.Width && desc.Height) {
                    const float ratio = (size.x / size.y) / (float(desc.Width) / desc.Height);
                    const float u = (1.0f - std::min(1.0f, ratio)) * 0.5f;
                    const float v = (1.0f - std::min(1.0f, 1.0f / ratio)) * 0.5f;
                    const ImVec2 end(pos.x + size.x, pos.y + size.y);
                    auto* draw = ImGui::GetWindowDrawList();
                    draw->AddImage(reinterpret_cast<ImTextureID>(texture), pos, end, ImVec2(u, v), ImVec2(1-u, 1-v));
                    const float veil = std::clamp(static_cast<float>(number_property(context, props, "backgroundVeil", 0)), 0.0f, 1.0f);
                    if (veil > 0) draw->AddRectFilled(pos, end, IM_COL32(255, 255, 255, static_cast<int>(255 * veil)));
                }
                image->Release();
            }
            resource->Release();
        }
    }
#endif
    if (has_menu) draw_menu_bar(session, menu);
    JS_FreeValue(context, menu);
    const ImVec2 window_pos = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    const float top_gap = has_menu ? 4.0f : padding;
    const float top = ImGui::GetCursorScreenPos().y + top_gap;
    const float page_w = std::max(1.0f, window_size.x - padding * 2.0f);
    const float page_h = std::max(1.0f, window_pos.y + window_size.y - padding - top);
    const ImVec2 origin(window_pos.x + padding, top);
    ImGui::SetCursorScreenPos(origin);
    // Let ImGui route the wheel once to the hovered scrollable window.
    ImGui::BeginChild("##ykm-page", ImVec2(page_w, page_h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar |
                          overflow_window_flags(context, props));
    rects_[session.id() + "\n" + path] = {origin.x, origin.y, page_w, page_h};
    {
        const ControlStyleScope style(context, props);
        render_children(session, children, 1, path);
    }
    ImGui::EndChild();
    ImGui::End();
    JS_FreeValue(context, children);
    JS_FreeValue(context, props);
}

void UiRenderer::draw_menu_bar(Session& session, JSValueConst menu) {
    ImFont* font = ImGui::GetFont();
    const float previous_scale = font ? font->Scale : 1.0f;
    if (font) font->Scale = previous_scale * 0.8f;
    const float bar_h = ImGui::GetTextLineHeight() + 6.0f;
    const ImVec2 window_pos = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(
        window_pos,
        ImVec2(window_pos.x + window_size.x, window_pos.y + bar_h),
        IM_COL32(255, 255, 255, 51));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0.14f));
    ImGui::SetCursorScreenPos(ImVec2(window_pos.x, window_pos.y + 1.0f));
    if (ImGui::Button("选项")) ImGui::OpenPopup("##ykm-menu-options");
    if (ImGui::BeginPopup("##ykm-menu-options")) {
        if (current_host_) {
            if (ImGui::MenuItem("放大")) current_host_->zoom = std::min(2.0f, current_host_->zoom * 1.125f);
            if (ImGui::MenuItem("缩小")) current_host_->zoom = std::max(0.5f, current_host_->zoom / 1.125f);
            if (ImGui::MenuItem("重置缩放")) current_host_->zoom = 1.0f;
            ImGui::Separator();
        }
        const bool can = current_host_ && current_host_->system &&
                         current_host_->system->capabilities().window_states;
        auto report = [](const char* operation, SystemResult result) {
            if (result != SystemResult::Ok)
                debug_log(LogLevel::Error, std::string(operation) +
                    (result == SystemResult::Unsupported ? ": unsupported" : ": platform request failed"));
        };
        if (ImGui::MenuItem("最大化", nullptr, false, can) && can) report("maximize", current_host_->system->maximize());
        if (ImGui::MenuItem("最小化", nullptr, false, can) && can) report("minimize", current_host_->system->minimize());
        if (ImGui::MenuItem("还原", nullptr, false, can) && can) report("restore", current_host_->system->restore());
        ImGui::EndPopup();
    }
    ImGui::SameLine(0, 0.0f);
    if (ImGui::Button("关于")) ImGui::OpenPopup("##ykm-menu-about");
    if (ImGui::BeginPopup("##ykm-menu-about")) {
        const std::string package_label = "包名: " + (package_name_.empty() ? std::string("未知") : package_name_);
        const std::string version_label = "版本: " + (version_.empty() ? std::string("未知") : version_);
        ImGui::MenuItem(package_label.c_str(), nullptr, false, false);
        ImGui::MenuItem(version_label.c_str(), nullptr, false, false);
        if (!repository_.empty()) {
            ImGui::Separator();
            if (ImGui::MenuItem("访问官网")) {
#ifdef _WIN32
                int length = MultiByteToWideChar(CP_UTF8, 0, repository_.data(), static_cast<int>(repository_.size()), nullptr, 0);
                std::wstring url(static_cast<size_t>(std::max(0, length)), L'\0');
                if (length > 0) MultiByteToWideChar(CP_UTF8, 0, repository_.data(), static_cast<int>(repository_.size()), url.data(), length);
                ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
                SDL_OpenURL(repository_.c_str());
#endif
            }
        }
        ImGui::EndPopup();
    }
    if (JS_IsArray(menu)) {
        ImGui::SameLine(0, 0.0f);
        draw_menu_items(session, menu, 0);
    }
    ImGui::SetCursorScreenPos(ImVec2(window_pos.x, window_pos.y + bar_h));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    if (font) font->Scale = previous_scale;
}

void UiRenderer::draw_menu_items(Session& session, JSValueConst items, int depth) {
    if (depth > 6 || !JS_IsArray(items)) return;
    JSContext* context = session.context();
    uint32_t length = 0;
    JSValue length_value = JS_GetPropertyStr(context, items, "length");
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);
    for (uint32_t index = 0; index < length; ++index) {
        JSValue item = JS_GetPropertyUint32(context, items, index);
        if (JS_IsString(item)) {
            const char* text = JS_ToCString(context, item);
            if (text && std::string(text) == "-") ImGui::Separator();
            else if (text) ImGui::MenuItem(text, nullptr, false, false);
            if (text) JS_FreeCString(context, text);
            JS_FreeValue(context, item);
            continue;
        }
        if (!JS_IsObject(item)) {
            JS_FreeValue(context, item);
            continue;
        }
        const std::string label = string_property(context, item, "label");
        if (label.empty() || label == "-") {
            ImGui::Separator();
            JS_FreeValue(context, item);
            continue;
        }
        const bool enabled = bool_property(context, item, "enabled", true);
        const std::string shortcut = string_property(context, item, "shortcut");
        JSValue children = JS_GetPropertyStr(context, item, "items");
        if (JS_IsArray(children)) {
            if (depth == 0) {
                const std::string popup = std::string("##ykm-menu-") + label;
                if (ImGui::Button(label.c_str())) ImGui::OpenPopup(popup.c_str());
                if (ImGui::BeginPopup(popup.c_str())) {
                    draw_menu_items(session, children, depth + 1);
                    ImGui::EndPopup();
                }
                ImGui::SameLine(0, 0.0f);
            } else if (ImGui::BeginMenu(label.c_str(), enabled)) {
                draw_menu_items(session, children, depth + 1);
                ImGui::EndMenu();
            }
        } else if (depth == 0) {
            if (ImGui::Button(label.c_str()) && enabled) {
                JSValue callback = callback_property(context, item, "onClick");
                if (!JS_IsUndefined(callback)) queue_event(context, callback, JS_UNDEFINED);
            }
            ImGui::SameLine(0, 0.0f);
        } else if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false, enabled)) {
            JSValue callback = callback_property(context, item, "onClick");
            if (!JS_IsUndefined(callback)) queue_event(context, callback, JS_UNDEFINED);
        }
        JS_FreeValue(context, children);
        JS_FreeValue(context, item);
    }
}

bool UiRenderer::render_window_control(RenderControlContext& control) {
    if (skip_window_nodes_) return false;
    const std::string title = string_property(control.js, control.props, "title", control.session.id());
    if (control.has_position) ImGui::SetNextWindowPos(control.position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(control.width > 0 ? control.width : 420.0f, control.height > 0 ? control.height : 280.0f),
        ImGuiCond_FirstUseEver);
    const bool open = ImGui::Begin(title.c_str());
    const ImVec2 window_position = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    rects_[control.scoped_key] = {window_position.x, window_position.y, window_size.x, window_size.y};
    draw_control_background(control.js, control.props, window_position, window_size);
    if (open) {
        const float padding = canvas_px(static_cast<float>(number_property(control.js, control.props, "padding", 0)));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(origin.x + padding, origin.y + padding));
        render_children(control.session, control.children, control.depth + 1, control.key);
    }
    ImGui::End();
    return true;
}

bool UiRenderer::render_column_control(RenderControlContext& control) {
    const bool scrolled = begin_overflow(control.js, control.props, control.key, control.width, control.height);
    const float gap = canvas_px(static_cast<float>(number_property(control.js, control.props, "gap", 0)));
    if (control.width > 0) ImGui::PushItemWidth(control.width);
    if (JS_IsArray(control.children)) {
        uint32_t length = 0;
        JSValue length_value = JS_GetPropertyStr(control.js, control.children, "length");
        JS_ToUint32(control.js, &length, length_value);
        JS_FreeValue(control.js, length_value);
        for (uint32_t index = 0; index < length; ++index) {
            if (index > 0 && gap > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);
            JSValue child = JS_GetPropertyUint32(control.js, control.children, index);
            render_node(control.session, child, control.depth + 1, control.key + "/" + std::to_string(index));
            JS_FreeValue(control.js, child);
        }
    }
    if (control.width > 0) ImGui::PopItemWidth();
    if (scrolled) ImGui::EndChild();
    return false;
}

bool UiRenderer::render_page_control(RenderControlContext& control) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float width = control.width > 0 ? control.width : std::max(1.0f, available.x);
    const float height = control.height > 0 ? control.height : std::max(1.0f, available.y);
    ImGui::BeginChild(("##page-" + control.key).c_str(), ImVec2(width, height), ImGuiChildFlags_None,
                      overflow_window_flags(control.js, control.props));
    const float gap = canvas_px(static_cast<float>(number_property(control.js, control.props, "gap", 0)));
    if (JS_IsArray(control.children)) {
        const uint32_t length = std::min<uint32_t>(array_length(control.js, control.children), kMaxNodes);
        for (uint32_t index = 0; index < length; ++index) {
            if (index > 0 && gap > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);
            JSValue child = JS_GetPropertyUint32(control.js, control.children, index);
            render_node(control.session, child, control.depth + 1, control.key + "/" + std::to_string(index));
            JS_FreeValue(control.js, child);
        }
    }
    ImGui::EndChild();
    return false;
}

bool UiRenderer::render_row_control(RenderControlContext& control) {
    const bool scrolled = begin_overflow(control.js, control.props, control.key, control.width, control.height);
    if (JS_IsArray(control.children)) {
        uint32_t length = 0;
        JSValue length_value = JS_GetPropertyStr(control.js, control.children, "length");
        JS_ToUint32(control.js, &length, length_value);
        JS_FreeValue(control.js, length_value);
        const float gap = has_property(control.js, control.props, "gap")
            ? canvas_px(static_cast<float>(number_property(control.js, control.props, "gap", 0)))
            : -1.0f;
        const float effective_gap = gap >= 0.0f ? gap : ImGui::GetStyle().ItemSpacing.x;
        const bool wrap = bool_property(control.js, control.props, "wrap", false);
        float line_width = 0.0f;
        for (uint32_t index = 0; index < length; ++index) {
            JSValue child = JS_GetPropertyUint32(control.js, control.children, index);
            bool same_line = index > 0;
            if (index > 0) {
                float next_width = canvas_px(80.0f);
                if (JS_IsObject(child)) {
                    JSValue child_props = JS_GetPropertyStr(control.js, child, "props");
                    next_width = canvas_px(static_cast<float>(number_property(control.js, child_props, "width", 80.0)));
                    JS_FreeValue(control.js, child_props);
                }
                same_line = !wrap || control.width <= 0.0f ||
                    line_width + effective_gap + next_width <= control.width;
                if (same_line) ImGui::SameLine(0, gap);
            }
            render_node(control.session, child, control.depth + 1, control.key + "/" + std::to_string(index));
            JS_FreeValue(control.js, child);
            const float item_width = ImGui::GetItemRectSize().x;
            line_width = same_line ? line_width + effective_gap + item_width : item_width;
        }
    }
    if (scrolled) ImGui::EndChild();
    return false;
}

bool UiRenderer::render_container_control(RenderControlContext& control) {
    const bool panel = control.type == "panel";
    if (panel) {
        const float pad = canvas_px(static_cast<float>(number_property(control.js, control.props, "padding", 8)));
        const float child_w = control.width > 0 ? control.width : ImGui::GetContentRegionAvail().x;
        float child_h = control.height > 0 ? control.height : ImGui::GetContentRegionAvail().y;
        if (child_h < canvas_px(48.0f)) child_h = canvas_px(120.0f);
        JSValue background = JS_GetPropertyStr(control.js, control.props, "background");
        const bool custom_background = JS_IsString(background) || JS_IsObject(background);
        JS_FreeValue(control.js, background);
        if (custom_background) {
            draw_control_background(control.js, control.props, ImGui::GetCursorScreenPos(), ImVec2(child_w, child_h));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, custom_background
            ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
            : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::BeginChild(control.key.c_str(), ImVec2(child_w, child_h), ImGuiChildFlags_None,
                          ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_HorizontalScrollbar |
                              overflow_window_flags(control.js, control.props));
    } else {
        ImGui::BeginGroup();
    }
    render_children(control.session, control.children, control.depth + 1, control.key);
    if (panel) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    } else {
        ImGui::EndGroup();
    }
    return false;
}

bool UiRenderer::render_modal_control(RenderControlContext& control) {
    const std::string title = string_property(control.js, control.props, "title", "Modal");
    const std::string id = title + "##modal-" + control.key;
    const bool has_open = has_property(control.js, control.props, "open");
    bool open = bool_property(control.js, control.props, "open", true);
    if (!has_open) {
        const auto found = popup_open_.find(control.scoped_key);
        if (found != popup_open_.end()) open = found->second;
    }
    if (open) ImGui::OpenPopup(id.c_str());
    ImGuiWindowFlags flags = 0;
    if (control.width > 0 || control.height > 0) {
        ImGui::SetNextWindowSize(
            ImVec2(control.width > 0 ? control.width : 420.0f, control.height > 0 ? control.height : 280.0f),
            ImGuiCond_Appearing);
    } else {
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
    }
    bool staying = open;
    if (ImGui::BeginPopupModal(id.c_str(), &staying, flags)) {
        const float pad = canvas_px(static_cast<float>(number_property(control.js, control.props, "padding", 8)));
        ImGui::Dummy(ImVec2(pad, 0));
        render_children(control.session, control.children, control.depth + 1, control.key);
        ImGui::EndPopup();
    }
    if (open && !staying) {
        popup_open_[control.scoped_key] = false;
        JSValue callback = callback_property(control.js, control.props, "onClose");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_UNDEFINED);
    } else if (has_open) {
        popup_open_[control.scoped_key] = open;
    }
    return false;
}

bool UiRenderer::render_graphics_control(RenderControlContext& control) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 size(control.width > 0 ? control.width : std::max(1.0f, available.x),
                      control.height > 0 ? control.height : std::max(ImGui::GetFrameHeight(), available.y));
    draw_control_background(control.js, control.props, ImGui::GetCursorScreenPos(), size);
    ImGui::InvisibleButton(("##graphics-" + control.key).c_str(), size);
    const ImVec2 min = ImGui::GetItemRectMin();
    JSValue drawing = JS_GetPropertyStr(control.js, control.props, "drawing");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(min, ImGui::GetItemRectMax(), true);
    draw_vector_commands(control.js, drawing, min);
    draw->PopClipRect();
    JS_FreeValue(control.js, drawing);
    render_children(control.session, control.children, control.depth + 1, control.key);
    return false;
}

bool UiRenderer::render_text_control(RenderControlContext& control) {
    const std::string text = string_property(control.js, control.props, "text");
    if (string_property(control.js, control.props, "textAlign") == "center") {
        const float width = control.width > 0 ? control.width : ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (width - ImGui::CalcTextSize(text.c_str()).x) * 0.5f));
    }
    if (bool_property(control.js, control.props, "wrap", false)) {
        const float wrap = control.width > 0
            ? ImGui::GetCursorPosX() + control.width
            : 0.0f;
        ImGui::PushTextWrapPos(wrap);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextUnformatted(text.c_str());
    }
    return false;
}

bool UiRenderer::render_tree_indent_control(RenderControlContext& control) {
    const int levels = std::max(0, static_cast<int>(number_property(control.js, control.props, "levels", 0)));
    const float step = canvas_px(22.0f);
    const ImVec2 size(control.width > 0 ? control.width : levels * step,
                      control.height > 0 ? control.height : ImGui::GetFrameHeight());
    ImGui::InvisibleButton(("##indent-" + control.key).c_str(), size);
    if (levels > 0) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 color = ImGui::GetColorU32(ImVec4(0.45f, 0.52f, 0.61f, 0.48f));
        const float extension = canvas_px(6.0f);
        for (int level = 0; level < levels; ++level) {
            const float x = min.x + step * level + step * 0.5f;
            draw->AddLine(ImVec2(x, min.y - extension), ImVec2(x, max.y + extension), color, 1.0f);
        }
    }
    return false;
}

bool UiRenderer::render_type_badge_control(RenderControlContext& control) {
    const std::string label = string_property(control.js, control.props, "label");
    const std::string display_label = label == "boolean" ? "bool" : label;
    const auto type_color = [](const std::string& name) {
        if (name == "string") return ImVec4(0.18f, 0.72f, 0.48f, 1.0f);
        if (name == "number") return ImVec4(0.30f, 0.56f, 0.94f, 1.0f);
        if (name == "boolean") return ImVec4(0.66f, 0.40f, 0.91f, 1.0f);
        if (name == "array") return ImVec4(0.94f, 0.58f, 0.20f, 1.0f);
        if (name == "object") return ImVec4(0.13f, 0.67f, 0.75f, 1.0f);
        return ImVec4(0.45f, 0.50f, 0.58f, 1.0f);
    };
    const ImVec4 color = type_color(label);
    const ImVec2 label_size = ImGui::CalcTextSize(display_label.c_str());
    const ImVec2 size(control.width > 0 ? control.width : label_size.x + canvas_px(16.0f),
                      control.height > 0 ? control.height : ImGui::GetFrameHeight());
    ImGui::InvisibleButton(("##type-" + control.key).c_str(), size);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float alpha = ImGui::IsItemHovered() ? 0.16f : 0.07f;
    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha)), 4.0f);
    draw->AddRect(min, max, ImGui::GetColorU32(color), 4.0f, 0, 1.5f);
    const ImVec2 text_size = ImGui::CalcTextSize(display_label.c_str());
    draw->AddText(ImVec2(min.x + (size.x - text_size.x) * 0.5f, min.y + (size.y - text_size.y) * 0.5f),
                  ImGui::GetColorU32(color), display_label.c_str());
    const std::string popup = "选择类型##" + control.key;
    if (ImGui::IsItemClicked()) ImGui::OpenPopup(popup.c_str());
    ImGui::SetNextWindowSize(ImVec2(canvas_px(220.0f), 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("选择 JSON 类型");
        ImGui::Separator();
        static const char* choices[][2] = {
            {"string", "string"}, {"number", "number"}, {"boolean", "bool"},
            {"array", "array"}, {"object", "object"}, {"null", "null"},
        };
        for (const auto& choice : choices) {
            ImGui::PushStyleColor(ImGuiCol_Text, type_color(choice[0]));
            if (ImGui::Selectable(choice[1], label == choice[0])) {
                JSValue callback = callback_property(control.js, control.props, "onChange");
                if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewString(control.js, choice[0]));
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        if (ImGui::Button("取消", ImVec2(-1.0f, 0.0f))) ImGui::CloseCurrentPopup();
        const ImVec2 popup_min = ImGui::GetWindowPos();
        const ImVec2 popup_max(popup_min.x + ImGui::GetWindowSize().x,
                               popup_min.y + ImGui::GetWindowSize().y);
        if (!ImGui::IsWindowAppearing() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseHoveringRect(popup_min, popup_max, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return false;
}

bool UiRenderer::render_disclosure_control(RenderControlContext& control) {
    const bool expanded = bool_property(control.js, control.props, "expanded", true);
    const bool empty = bool_property(control.js, control.props, "empty", false);
    const ImVec2 size(control.width > 0 ? control.width : canvas_px(22.0f),
                      control.height > 0 ? control.height : ImGui::GetFrameHeight());
    ImGui::InvisibleButton(("##disclosure-" + control.key).c_str(), size);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 center(min.x + size.x * 0.5f, min.y + size.y * 0.5f);
    const float r = canvas_px(7.0f);
    const ImU32 color = ImGui::GetColorU32(expanded
        ? ImVec4(0.20f, 0.72f, 0.48f, 1.0f)
        : ImVec4(0.95f, 0.62f, 0.20f, 1.0f));
    if (empty) {
        ImGui::GetWindowDrawList()->AddCircleFilled(
            center, canvas_px(3.0f), ImGui::GetColorU32(ImVec4(0.52f, 0.55f, 0.61f, 1.0f)));
    } else if (expanded) {
        ImGui::GetWindowDrawList()->AddTriangleFilled(
            ImVec2(center.x - r, center.y - r * 0.55f), ImVec2(center.x + r, center.y - r * 0.55f),
            ImVec2(center.x, center.y + r), color);
    } else {
        ImGui::GetWindowDrawList()->AddTriangleFilled(
            ImVec2(center.x - r * 0.55f, center.y - r), ImVec2(center.x - r * 0.55f, center.y + r),
            ImVec2(center.x + r, center.y), color);
    }
    if (ImGui::IsItemClicked()) {
        JSValue callback = callback_property(control.js, control.props, "onClick");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_UNDEFINED);
    }
    return false;
}

bool UiRenderer::render_icon_button_control(RenderControlContext& control) {
    const std::string icon = string_property(control.js, control.props, "icon", "plus");
    const bool add = icon == "plus";
    const ImVec4 color = add ? ImVec4(0.18f, 0.76f, 0.42f, 1.0f) : ImVec4(0.92f, 0.25f, 0.28f, 1.0f);
    const float frame_height = ImGui::GetFrameHeight();
    const ImVec2 size(control.width > 0 ? control.width : frame_height,
                      control.height > 0 ? control.height : frame_height);
    ImGui::InvisibleButton(("##icon-" + control.key).c_str(), size);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z,
                        ImGui::IsItemHovered() ? 0.14f : 0.05f)), 4.0f);
    draw->AddRect(min, max, ImGui::GetColorU32(color), 4.0f, 0, 1.5f);
    const float r = canvas_px(5.0f);
    const ImU32 packed = ImGui::GetColorU32(color);
    if (add) {
        draw->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y), packed, 2.0f);
        draw->AddLine(ImVec2(center.x, center.y - r), ImVec2(center.x, center.y + r), packed, 2.0f);
    } else {
        draw->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), packed, 2.0f);
        draw->AddLine(ImVec2(center.x + r, center.y - r), ImVec2(center.x - r, center.y + r), packed, 2.0f);
    }
    if (ImGui::IsItemClicked()) {
        JSValue callback = callback_property(control.js, control.props, "onClick");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_UNDEFINED);
    }
    return false;
}

bool UiRenderer::render_button_control(RenderControlContext& control) {
    const std::string label = string_property(control.js, control.props, "label", "Button");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button(label.c_str(), ImVec2(control.width, control.height > 0 ? control.height : canvas_px(36.0f)))) {
        JSValue callback = callback_property(control.js, control.props, "onClick");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_UNDEFINED);
    }
    ImGui::PopStyleColor();
    return false;
}

bool UiRenderer::render_checkbox_control(RenderControlContext& control) {
    const std::string label = string_property(control.js, control.props, "label", "");
    bool value = bool_property(control.js, control.props, "value", false);
    if (ImGui::Checkbox(label.c_str(), &value)) {
        JSValue callback = callback_property(control.js, control.props, "onChange");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewBool(control.js, value));
    }
    return false;
}

bool UiRenderer::render_input_control(RenderControlContext& control) {
    const std::string label = string_property(control.js, control.props, "label", "");
    const std::string supplied = string_property(control.js, control.props, "value", input_values_[control.key]);
    std::string& stored = input_values_[control.key];
    if (stored != supplied) stored = supplied;
    const bool read_only = bool_property(control.js, control.props, "readOnly", false);
    // Read-only text is supplied by the app, not typed into this buffer. Preserve it in full.
    const size_t max_length = read_only ? stored.size() : static_cast<size_t>(
        std::clamp(number_property(control.js, control.props, "maxLength", 1023), 1.0, 1024.0 * 1024));
    if (stored.size() > max_length) stored.resize(max_length);
    std::vector<char> buffer(max_length + 1, 0);
    std::snprintf(buffer.data(), buffer.size(), "%s", stored.c_str());
    ImGuiInputTextFlags flags = 0;
    if (read_only) flags |= ImGuiInputTextFlags_ReadOnly;
    if (bool_property(control.js, control.props, "password", false)) flags |= ImGuiInputTextFlags_Password;
    bool adjusted_padding = false;
    if (control.height > ImGui::GetFrameHeight()) {
        const float extra = (control.height - ImGui::GetFrameHeight()) * 0.5f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y + extra));
        adjusted_padding = true;
    }
    if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size(), flags)) {
        stored = buffer.data();
        JSValue callback = callback_property(control.js, control.props, "onChange");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewString(control.js, stored.c_str()));
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        JSValue callback = callback_property(control.js, control.props, "onSubmit");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewString(control.js, stored.c_str()));
    }
    if (adjusted_padding) ImGui::PopStyleVar();
    return false;
}

bool UiRenderer::render_slider_control(RenderControlContext& control) {
    const std::string label = string_property(control.js, control.props, "label", "");
    float value = static_cast<float>(number_property(control.js, control.props, "value", 0));
    const float minimum = static_cast<float>(number_property(control.js, control.props, "min", 0));
    const float maximum = static_cast<float>(number_property(control.js, control.props, "max", 100));
    if (ImGui::SliderFloat(label.c_str(), &value, minimum, maximum)) {
        JSValue callback = callback_property(control.js, control.props, "onChange");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewFloat64(control.js, value));
    }
    return false;
}

bool UiRenderer::render_separator_control(RenderControlContext&) {
    ImGui::Separator();
    return false;
}

bool UiRenderer::render_select_control(RenderControlContext& control) {
    const std::vector<SelectOption> options = read_options(control.js, control.props);
    const std::string value = string_property(control.js, control.props, "value");
    std::string preview = value;
    for (const SelectOption& option : options) {
        if (option.value == value) {
            preview = option.label.empty() ? option.value : option.label;
            break;
        }
    }
    const std::string label = string_property(control.js, control.props, "label");
    const std::string id = label.empty() ? "##select-" + control.key : label + "##select-" + control.key;
    if (ImGui::BeginCombo(id.c_str(), preview.c_str())) {
        for (const SelectOption& option : options) {
            const bool selected = option.value == value;
            if (ImGui::Selectable(option.label.empty() ? option.value.c_str() : option.label.c_str(), selected) &&
                !selected) {
                JSValue callback = callback_property(control.js, control.props, "onChange");
                if (!JS_IsUndefined(callback)) {
                    queue_event(control.js, callback, JS_NewString(control.js, option.value.c_str()));
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return false;
}

bool UiRenderer::render_radio_control(RenderControlContext& control) {
    const std::vector<SelectOption> options = read_options(control.js, control.props);
    const std::string value = string_property(control.js, control.props, "value");
    const std::string direction = string_property(control.js, control.props, "direction", "column");
    const bool horizontal = direction == "row";
    for (size_t index = 0; index < options.size(); ++index) {
        const SelectOption& option = options[index];
        if (horizontal && index > 0) ImGui::SameLine();
        const bool selected = option.value == value;
        const std::string id = (option.label.empty() ? option.value : option.label) + "##radio-" +
            control.key + "-" + std::to_string(index);
        if (ImGui::RadioButton(id.c_str(), selected) && !selected) {
            JSValue callback = callback_property(control.js, control.props, "onChange");
            if (!JS_IsUndefined(callback)) {
                queue_event(control.js, callback, JS_NewString(control.js, option.value.c_str()));
            }
        }
    }
    return false;
}

bool UiRenderer::render_tabs_control(RenderControlContext& control) {
    const std::vector<SelectOption> items = read_options(control.js, control.props);
    std::string value = string_property(control.js, control.props, "value");
    if (value.empty() && !items.empty()) value = items.front().value;
    if (!items.empty() && ImGui::BeginTabBar(("##tabs-" + control.key).c_str())) {
        for (const SelectOption& item : items) {
            const bool selected = item.value == value;
            ImGuiTabItemFlags flags = selected ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(item.label.empty() ? item.value.c_str() : item.label.c_str(), nullptr, flags)) {
                if (!selected) {
                    JSValue callback = callback_property(control.js, control.props, "onChange");
                    if (!JS_IsUndefined(callback)) {
                        queue_event(control.js, callback, JS_NewString(control.js, item.value.c_str()));
                    }
                    value = item.value;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    if (!JS_IsArray(control.children)) {
        render_children(control.session, control.children, control.depth + 1, control.key);
        return false;
    }
    const uint32_t length = std::min<uint32_t>(array_length(control.js, control.children), kMaxNodes);
    for (uint32_t index = 0; index < length; ++index) {
        JSValue child = JS_GetPropertyUint32(control.js, control.children, index);
        JSValue child_props = JS_GetPropertyStr(control.js, child, "props");
        const std::string tab = string_property(control.js, child_props, "tab");
        JS_FreeValue(control.js, child_props);
        if (tab.empty() || tab == value) {
            render_node(control.session, child, control.depth + 1, control.key + "/" + std::to_string(index));
        }
        JS_FreeValue(control.js, child);
    }
    return false;
}

bool UiRenderer::render_list_control(RenderControlContext& control) {
    const std::vector<SelectOption> options = read_options(control.js, control.props);
    const bool multiple = bool_property(control.js, control.props, "multiple", false);
    std::vector<std::string> selected = read_values(control.js, control.props);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float height = control.height > 0
        ? control.height
        : std::min(available.y, ImGui::GetFrameHeight() * std::max<size_t>(1, std::min(options.size(), size_t{8})));
    const float width = control.width > 0 ? control.width : std::max(1.0f, available.x);
    ImGui::BeginChild(("##list-" + control.key).c_str(), ImVec2(width, height), ImGuiChildFlags_Borders);
    for (const SelectOption& option : options) {
        const bool is_selected = std::find(selected.begin(), selected.end(), option.value) != selected.end();
        if (ImGui::Selectable(option.label.empty() ? option.value.c_str() : option.label.c_str(), is_selected)) {
            JSValue callback = callback_property(control.js, control.props, "onChange");
            if (!JS_IsUndefined(callback)) {
                if (multiple) {
                    auto next = selected;
                    const auto found = std::find(next.begin(), next.end(), option.value);
                    if (found == next.end()) next.push_back(option.value);
                    else next.erase(found);
                    JSValue values = JS_NewArray(control.js);
                    for (uint32_t index = 0; index < next.size(); ++index) {
                        JS_SetPropertyUint32(control.js, values, index, JS_NewString(control.js, next[index].c_str()));
                    }
                    queue_event(control.js, callback, values);
                } else if (!is_selected) {
                    queue_event(control.js, callback, JS_NewString(control.js, option.value.c_str()));
                }
            }
        }
    }
    ImGui::EndChild();
    return false;
}

bool UiRenderer::render_scroll_control(RenderControlContext& control) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float width = control.width > 0 ? control.width : std::max(1.0f, available.x);
    const float height = control.height > 0 ? control.height : std::max(1.0f, available.y);
    ImGuiWindowFlags flags = 0;
    if (string_property(control.js, control.props, "overflow") == "scroll") {
        flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
    }
    ImGui::BeginChild(("##scroll-" + control.key).c_str(), ImVec2(width, height), ImGuiChildFlags_None, flags);
    render_children(control.session, control.children, control.depth + 1, control.key);
    ImGui::EndChild();
    return false;
}

bool UiRenderer::render_progress_control(RenderControlContext& control) {
    const float minimum = static_cast<float>(number_property(control.js, control.props, "min", 0));
    const float maximum = static_cast<float>(number_property(control.js, control.props, "max", 1));
    const float value = static_cast<float>(number_property(control.js, control.props, "value", 0));
    const float span = maximum - minimum;
    const float fraction = span > 0 ? std::clamp((value - minimum) / span, 0.0f, 1.0f) : 0.0f;
    const std::string overlay = string_property(control.js, control.props, "label");
    ImGui::ProgressBar(fraction, ImVec2(control.width > 0 ? control.width : -1.0f,
                                        control.height > 0 ? control.height : 0.0f),
                       overlay.empty() ? nullptr : overlay.c_str());
    return false;
}

UiRenderer::~UiRenderer() {
#ifdef _WIN32
    for (const auto& entry : image_textures_) {
        if (entry.second) static_cast<ID3D11ShaderResourceView*>(entry.second)->Release();
    }
#else
    for (const auto& entry : image_textures_) SDL_DestroyTexture(entry.second);
#endif
}

bool UiRenderer::render_text_area_control(RenderControlContext& control) {
    const std::string label = string_property(control.js, control.props, "label");
    const std::string supplied = string_property(control.js, control.props, "value", input_values_[control.key]);
    std::string& stored = input_values_[control.key];
    if (stored != supplied) stored = supplied;
    const size_t max_length = static_cast<size_t>(std::max(1.0, number_property(control.js, control.props, "maxLength", 1024 * 1024)));
    if (stored.size() > max_length) stored.resize(max_length);
    std::vector<char> buffer(std::max<size_t>(stored.size() + 256, std::min<size_t>(max_length + 1, 1024 * 1024 + 1)), 0);
    std::snprintf(buffer.data(), buffer.size(), "%s", stored.c_str());
    const float height = control.height > 0 ? control.height : canvas_px(120.0f);
    ImGuiInputTextFlags flags = 0;
    if (bool_property(control.js, control.props, "readOnly", false)) flags |= ImGuiInputTextFlags_ReadOnly;
    if (bool_property(control.js, control.props, "password", false)) flags |= ImGuiInputTextFlags_Password;
    if (ImGui::InputTextMultiline(label.c_str(), buffer.data(), buffer.size(),
                                  ImVec2(control.width > 0 ? control.width : -1.0f, height), flags)) {
        stored = buffer.data();
        JSValue callback = callback_property(control.js, control.props, "onChange");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewString(control.js, stored.c_str()));
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        JSValue callback = callback_property(control.js, control.props, "onSubmit");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewString(control.js, stored.c_str()));
    }
    return false;
}

bool UiRenderer::render_number_input_control(RenderControlContext& control) {
    const std::string label = string_property(control.js, control.props, "label");
    float value = static_cast<float>(number_property(control.js, control.props, "value", 0));
    const float step = static_cast<float>(number_property(control.js, control.props, "step", 1));
    const float step_fast = static_cast<float>(number_property(control.js, control.props, "stepFast", step * 10));
    const bool has_min = has_property(control.js, control.props, "min");
    const bool has_max = has_property(control.js, control.props, "max");
    const float minimum = static_cast<float>(number_property(control.js, control.props, "min", value));
    const float maximum = static_cast<float>(number_property(control.js, control.props, "max", value));
    if (has_min) value = std::max(value, minimum);
    if (has_max) value = std::min(value, maximum);
    if (ImGui::InputFloat(label.c_str(), &value, step, step_fast)) {
        if (has_min) value = std::max(value, minimum);
        if (has_max) value = std::min(value, maximum);
        JSValue callback = callback_property(control.js, control.props, "onChange");
        if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewFloat64(control.js, value));
    }
    return false;
}

#ifdef _WIN32
void* UiRenderer::load_win32_image(const std::string& source) {
    if (source.empty() || !current_host_ || !current_host_->system) return nullptr;
    const std::string cache_key = source;
    ID3D11ShaderResourceView* texture = nullptr;
    const auto cached = image_textures_.find(cache_key);
    if (cached != image_textures_.end()) texture = static_cast<ID3D11ShaderResourceView*>(cached->second);
    if (!texture) {
        IWICImagingFactory* imaging = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        IWICStream* stream = nullptr;
        int path_length = MultiByteToWideChar(CP_UTF8, 0, source.data(), static_cast<int>(source.size()), nullptr, 0);
        std::wstring path(static_cast<size_t>(std::max(0, path_length)), L'\0');
        if (path_length > 0) {
            MultiByteToWideChar(CP_UTF8, 0, source.data(), static_cast<int>(source.size()), path.data(), path_length);
        }
        bool ok = false;
        do {
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&imaging)))) break;
            if (source.rfind("asset:/", 0) == 0) {
                AssetView asset;
                const auto name = std::filesystem::u8path(source.substr(7)).lexically_normal();
                if (name.empty() || name.has_root_path() || *name.begin() == "..") break;
                if (embedded_asset(name.generic_string(), asset)) {
                    if (FAILED(imaging->CreateStream(&stream))) break;
                    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(asset.data), asset.size))) break;
                    if (FAILED(imaging->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) break;
                } else if (!app_allows("fs.asset") || !current_host_->session || FAILED(imaging->CreateDecoderFromFilename(
                    (current_host_->session->root() / "assets" / name).c_str(), nullptr, GENERIC_READ,
                    WICDecodeMetadataCacheOnLoad, &decoder))) break;
            } else if (FAILED(imaging->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                                    WICDecodeMetadataCacheOnLoad, &decoder))) break;
            if (FAILED(decoder->GetFrame(0, &frame))) break;
            if (FAILED(imaging->CreateFormatConverter(&converter))) break;
            if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                              WICBitmapDitherTypeNone, nullptr, 0.0,
                                              WICBitmapPaletteTypeCustom))) break;
            UINT image_width = 0, image_height = 0;
            converter->GetSize(&image_width, &image_height);
            std::vector<uint8_t> pixels(static_cast<size_t>(image_width) * image_height * 4);
            if (FAILED(converter->CopyPixels(nullptr, image_width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) break;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = image_width; desc.Height = image_height; desc.MipLevels = 1; desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{pixels.data(), image_width * 4, 0};
            ID3D11Texture2D* image = nullptr;
            if (FAILED(d3d11_renderer().device()->CreateTexture2D(&desc, &data, &image))) break;
            const HRESULT view_result = d3d11_renderer().device()->CreateShaderResourceView(image, nullptr, &texture);
            image->Release();
            ok = SUCCEEDED(view_result);
        } while (false);
        if (stream) stream->Release();
        if (converter) converter->Release(); if (frame) frame->Release();
        if (decoder) decoder->Release(); if (imaging) imaging->Release();
        if (ok) image_textures_[cache_key] = texture;
    }
    return texture;
}
#endif

bool UiRenderer::render_image_control(RenderControlContext& control) {
    const std::string source = string_property(control.js, control.props, "source",
        string_property(control.js, control.props, "src"));
    if (source.empty() || !current_host_ || !current_host_->system) {
        ImGui::TextDisabled("Image source unavailable");
        return false;
    }
#ifdef _WIN32
    auto* texture = static_cast<ID3D11ShaderResourceView*>(load_win32_image(source));
    if (!texture) { ImGui::TextDisabled("Image unavailable: %s", source.c_str()); return false; }
    float width = control.width, height = control.height;
    if (width <= 0 || height <= 0) { D3D11_RESOURCE_DIMENSION type{}; ID3D11Resource* resource = nullptr; texture->GetResource(&resource); ID3D11Texture2D* image = nullptr; if (resource && SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&image)))) { D3D11_TEXTURE2D_DESC desc{}; image->GetDesc(&desc); if (width <= 0) width = static_cast<float>(desc.Width); if (height <= 0) height = static_cast<float>(desc.Height); image->Release(); } if (resource) resource->Release(); }
    ImGui::Image(reinterpret_cast<ImTextureID>(texture), ImVec2(width, height));
    return false;
#else
    const std::string cache_key = source + "@" + std::to_string(
        reinterpret_cast<uintptr_t>(current_host_->system->native_handle(NativeHandleKind::Renderer)));
    SDL_Texture* texture = nullptr;
    const auto cached = image_textures_.find(cache_key);
    if (cached != image_textures_.end()) texture = cached->second;
    if (!texture) {
        SDL_Surface* surface = nullptr;
        if (source.rfind("asset:/", 0) == 0) {
            AssetView asset;
            if (embedded_asset(source.substr(7), asset)) {
                SDL_IOStream* stream = SDL_IOFromConstMem(asset.data, static_cast<size_t>(asset.size));
                if (stream) surface = IMG_Load_IO(stream, true);
            }
        } else {
            surface = IMG_Load(source.c_str());
        }
        if (surface) {
            SDL_Renderer* renderer = static_cast<SDL_Renderer*>(
                current_host_->system->native_handle(NativeHandleKind::Renderer));
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_DestroySurface(surface);
            if (texture) image_textures_[cache_key] = texture;
        }
    }
    if (!texture) {
        ImGui::TextDisabled("Image unavailable: %s", source.c_str());
        return false;
    }
    float width = control.width;
    float height = control.height;
    float texture_width = 0;
    float texture_height = 0;
    SDL_GetTextureSize(texture, &texture_width, &texture_height);
    if (width <= 0) width = texture_width;
    if (height <= 0) height = texture_height;
    ImGui::Image(reinterpret_cast<ImTextureID>(texture), ImVec2(width, height));
    return false;
#endif
}

bool UiRenderer::render_tree_control(RenderControlContext& control) {
    JSValue nodes = JS_GetPropertyStr(control.js, control.props, "nodes");
    if (!JS_IsArray(nodes)) {
        JS_FreeValue(control.js, nodes);
        return false;
    }
    const std::string selected = string_property(control.js, control.props, "selected");
    std::function<void(JSValueConst, int)> render_nodes = [&](JSValueConst list, int depth) {
        const uint32_t length = std::min<uint32_t>(array_length(control.js, list), 4096);
        for (uint32_t index = 0; index < length; ++index) {
            JSValue node = JS_GetPropertyUint32(control.js, list, index);
            if (!JS_IsObject(node)) {
                JS_FreeValue(control.js, node);
                continue;
            }
            const std::string id = string_property(control.js, node, "id", std::to_string(index));
            const std::string label = string_property(control.js, node, "label", id);
            const bool expanded = bool_property(control.js, node, "expanded", true);
            const bool disabled = bool_property(control.js, node, "disabled", false);
            JSValue children = JS_GetPropertyStr(control.js, node, "children");
            const bool has_children = JS_IsArray(children) && array_length(control.js, children) > 0;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf;
            if (id == selected) flags |= ImGuiTreeNodeFlags_Selected;
            if (has_property(control.js, node, "expanded")) {
                ImGui::SetNextItemOpen(expanded, ImGuiCond_Always);
            } else if (expanded) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            if (disabled) ImGui::BeginDisabled();
            const bool open = ImGui::TreeNodeEx((id + "##tree-" + control.key).c_str(), flags, "%s", label.c_str());
            if (ImGui::IsItemClicked() && !disabled) {
                JSValue callback = callback_property(control.js, control.props, "onSelect");
                if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewString(control.js, id.c_str()));
            }
            if (!disabled && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                JSValue callback = callback_property(control.js, node, "onDoubleClick");
                if (JS_IsUndefined(callback)) callback = callback_property(control.js, control.props, "onDoubleClick");
                if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewString(control.js, id.c_str()));
            }
            if (!disabled && ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                JSValue callback = callback_property(control.js, node, "onActivate");
                if (JS_IsUndefined(callback)) callback = callback_property(control.js, control.props, "onActivate");
                if (!JS_IsUndefined(callback)) queue_event(control.js, callback, JS_NewString(control.js, id.c_str()));
            }
            JSValue context_menu = JS_GetPropertyStr(control.js, node, "menu");
            if (!disabled && JS_IsArray(context_menu) && ImGui::BeginPopupContextItem((id + "##tree-menu-" + control.key).c_str())) {
                draw_menu_items(control.session, context_menu, 1);
                ImGui::EndPopup();
            }
            JS_FreeValue(control.js, context_menu);
            if (has_children && ImGui::IsItemToggledOpen()) {
                JSValue callback = callback_property(control.js, control.props, "onToggle");
                if (!JS_IsUndefined(callback)) {
                    JSValue event = JS_NewObject(control.js);
                    JS_SetPropertyStr(control.js, event, "id", JS_NewString(control.js, id.c_str()));
                    JS_SetPropertyStr(control.js, event, "expanded", JS_NewBool(control.js, open));
                    queue_event(control.js, callback, event);
                }
            }
            if (open && has_children) {
                render_nodes(children, depth + 1);
                ImGui::TreePop();
            }
            if (disabled) ImGui::EndDisabled();
            JS_FreeValue(control.js, children);
            JS_FreeValue(control.js, node);
        }
    };
    render_nodes(nodes, 0);
    JS_FreeValue(control.js, nodes);
    return false;
}

void UiRenderer::render_node(Session& session, JSValueConst node, int depth, const std::string& path) {
    JSContext* context = session.context();
    if (depth > kMaxDepth || ++node_count_ > kMaxNodes) {
        ImGui::TextUnformatted("Component tree limit exceeded");
        return;
    }
    if (JS_IsNull(node) || JS_IsUndefined(node) || JS_IsBool(node)) {
        return;
    }
    if (JS_IsString(node) || JS_IsNumber(node)) {
        const char* text = JS_ToCString(context, node);
        if (text) {
            ImGui::TextUnformatted(text);
            JS_FreeCString(context, text);
        }
        return;
    }
    if (!JS_IsObject(node)) {
        ImGui::TextDisabled("Invalid component at %s", path.c_str());
        return;
    }

    JSValue type_value = JS_GetPropertyStr(context, node, "type");
    const char* type_text = JS_ToCString(context, type_value);
    std::string type = type_text ? type_text : "";
    if (type_text) {
        JS_FreeCString(context, type_text);
    }
    JS_FreeValue(context, type_value);
    JSValue props = JS_GetPropertyStr(context, node, "props");
    JSValue children = JS_GetPropertyStr(context, node, "children");
    const std::string key = string_property(context, props, "key", path + "/" + type);
    const std::string scoped_key = session.id() + "\n" + key;
    if (!bool_property(context, props, "visible", true)) {
        JS_FreeValue(context, children);
        JS_FreeValue(context, props);
        return;
    }

    float width = has_property(context, props, "width")
        ? canvas_px(static_cast<float>(number_property(context, props, "width", 0)))
        : 0.0f;
    float height = has_property(context, props, "height")
        ? canvas_px(static_cast<float>(number_property(context, props, "height", 0)))
        : 0.0f;
    bool has_position = has_property(context, props, "x") || has_property(context, props, "y");
    ImVec2 position = ImGui::GetCursorScreenPos();
    if (has_property(context, props, "x")) {
        position.x = canvas_px(static_cast<float>(number_property(context, props, "x", 0)));
    }
    if (has_property(context, props, "y")) {
        position.y = canvas_px(static_cast<float>(number_property(context, props, "y", 0)));
    }

    JSValue relations = JS_GetPropertyStr(context, node, "relations");
    if (JS_IsArray( relations)) {
        uint32_t relation_count = 0;
        JSValue length = JS_GetPropertyStr(context, relations, "length");
        JS_ToUint32(context, &relation_count, length);
        JS_FreeValue(context, length);
        for (uint32_t index = 0; index < relation_count; ++index) {
            JSValue relation = JS_GetPropertyUint32(context, relations, index);
            const std::string target = string_property(context, relation, "target");
            const auto relation_type = static_cast<RelationType>(
                static_cast<int32_t>(number_property(context, relation, "type", 0)));
            const bool percent = bool_property(context, relation, "percent", false);
            const float raw_offset = static_cast<float>(number_property(context, relation, "offset", 0));
            const auto target_rect = rects_.find(session.id() + "\n" + target);
            if (target_rect != rects_.end()) {
                const Rect& target_box = target_rect->second;
                const float x_offset = percent ? target_box.width * raw_offset / 100.0f : canvas_px(raw_offset);
                const float y_offset = percent ? target_box.height * raw_offset / 100.0f : canvas_px(raw_offset);
                if (relation_type == RelationType::Width) {
                    width = percent ? target_box.width * raw_offset / 100.0f : target_box.width + canvas_px(raw_offset);
                } else if (relation_type == RelationType::Height) {
                    height = percent ? target_box.height * raw_offset / 100.0f : target_box.height + canvas_px(raw_offset);
                } else if (relation_type == RelationType::Size) {
                    width = percent ? target_box.width * raw_offset / 100.0f : target_box.width + canvas_px(raw_offset);
                    height = percent ? target_box.height * raw_offset / 100.0f : target_box.height + canvas_px(raw_offset);
                } else if (relation_type == RelationType::LeftLeft) {
                    position.x = target_box.x + x_offset;
                    has_position = true;
                } else if (relation_type == RelationType::LeftCenter) {
                    position.x = target_box.x + target_box.width * 0.5f + x_offset;
                    has_position = true;
                } else if (relation_type == RelationType::LeftRight) {
                    position.x = target_box.x + target_box.width + x_offset;
                    has_position = true;
                } else if (relation_type == RelationType::CenterCenter) {
                    position.x = target_box.x + (target_box.width - width) * 0.5f + x_offset;
                    has_position = true;
                } else if (relation_type == RelationType::RightLeft) {
                    position.x = target_box.x - width + x_offset;
                    has_position = true;
                } else if (relation_type == RelationType::RightRight) {
                    position.x = target_box.x + target_box.width - width + x_offset;
                    has_position = true;
                } else if (relation_type == RelationType::TopTop) {
                    position.y = target_box.y + y_offset;
                    has_position = true;
                } else if (relation_type == RelationType::TopMiddle) {
                    position.y = target_box.y + target_box.height * 0.5f + y_offset;
                    has_position = true;
                } else if (relation_type == RelationType::TopBottom) {
                    position.y = target_box.y + target_box.height + y_offset;
                    has_position = true;
                } else if (relation_type == RelationType::MiddleMiddle) {
                    position.y = target_box.y + (target_box.height - height) * 0.5f + y_offset;
                    has_position = true;
                } else if (relation_type == RelationType::BottomTop) {
                    position.y = target_box.y - height + y_offset;
                    has_position = true;
                } else if (relation_type == RelationType::BottomBottom) {
                    position.y = target_box.y + target_box.height - height + y_offset;
                    has_position = true;
                }
            }
            JS_FreeValue(context, relation);
        }
    }
    JS_FreeValue(context, relations);

    if (has_position && type != "window") ImGui::SetCursorScreenPos(position);
    if (width > 0 && type != "window") ImGui::SetNextItemWidth(width);
    const std::string font_name = string_property(context, props, "font");
    ImFont* font = font_name.empty() ? nullptr : fonts_get(font_name);
    if (font) ImGui::PushFont(font);

    const bool disabled = bool_property(context, props, "disabled", false);
    if (disabled) ImGui::BeginDisabled();

    ImGui::PushID(key.c_str());
    const ImVec2 start = ImGui::GetCursorScreenPos();
    JSValue background = JS_GetPropertyStr(context, props, "background");
    const bool has_background = JS_IsString(background) || JS_IsObject(background);
    JS_FreeValue(context, background);
    if (has_background && type != "window" && type != "panel" && type != "modal" && type != "graphics") {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 background_size(
            width > 0 ? width : std::max(1.0f, available.x),
            height > 0 ? height : ImGui::GetFrameHeight());
        draw_control_background(context, props, start, background_size);
    }
    bool window_node = false;
    if (const ControlRenderer renderer = control_renderer(type)) {
        RenderControlContext control{
            session, context, props, children, depth, type, key, scoped_key,
            width, height, has_position, position, start,
        };
        window_node = (this->*renderer)(control);
    } else {
        ImGui::TextDisabled("Unknown component: %s", type.c_str());
    }
    const std::string tooltip = string_property(context, props, "tooltip");
    if (!tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.11f, 0.15f, 0.21f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        if (ImGui::BeginTooltip()) {
            ImGui::PushTextWrapPos(std::min(canvas_px(360.0f), ImGui::GetIO().DisplaySize.x * 0.8f));
            ImGui::TextUnformatted(tooltip.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::PopStyleColor(2);
    }
    const ImVec2 end = ImGui::GetCursorScreenPos();
    if (!window_node) {
        const float measured_width = width > 0 ? width : std::max(0.0f, end.x - start.x);
        const float measured_height = height > 0 ? height : std::max(ImGui::GetTextLineHeight(), end.y - start.y);
        rects_[scoped_key] = {start.x, start.y, measured_width, measured_height};
    }
    ImGui::PopID();
    if (disabled) ImGui::EndDisabled();
    if (font) ImGui::PopFont();
    JS_FreeValue(context, children);
    JS_FreeValue(context, props);
}

void UiRenderer::render_children(Session& session, JSValueConst children, int depth, const std::string& path) {
    JSContext* context = session.context();
    if (!JS_IsArray( children)) {
        render_node(session, children, depth, path + "/0");
        return;
    }
    uint32_t length = 0;
    JSValue length_value = JS_GetPropertyStr(context, children, "length");
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);
    length = std::min<uint32_t>(length, kMaxNodes);
    for (uint32_t index = 0; index < length; ++index) {
        JSValue child = JS_GetPropertyUint32(context, children, index);
        render_node(session, child, depth, path + "/" + std::to_string(index));
        JS_FreeValue(context, child);
    }
}

void UiRenderer::queue_event(JSContext* context, JSValueConst callback, JSValue argument) {
    events_.push_back({context, JS_DupValue(context, callback), argument});
    JS_FreeValue(context, callback);
}

void UiRenderer::dispatch_events(Session& session) {
    for (Event& event : events_) {
        const int argc = JS_IsUndefined(event.argument) ? 0 : 1;
        JSValue result = JS_Call(event.context, event.callback, JS_UNDEFINED, argc, argc ? &event.argument : nullptr);
        JS_FreeValue(event.context, event.callback);
        JS_FreeValue(event.context, event.argument);
        if (JS_IsException(result)) {
            session.report_exception("UI event");
        }
        JS_FreeValue(event.context, result);
    }
    events_.clear();
}

} // namespace ykm
