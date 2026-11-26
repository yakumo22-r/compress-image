#pragma once

#include "host_windows.h"

#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <quickjs.h>
}

#ifndef _WIN32
struct SDL_Texture;
#endif

namespace ykm {

class Session;

class UiRenderer {
public:
    ~UiRenderer();
    void set_app_metadata(std::string package_name, std::string version, std::string repository);
    void collect_windows(Session& session, JSValueConst tree, std::vector<HostWindowRequest>& out);
    void render_named_window(Session& session, JSValueConst tree, const std::string& key, HostWindow* host);
    void render_tree(Session& session, JSValueConst tree, HostWindow* host);
    void finish(Session& session);

private:
    struct RenderControlContext;
    using ControlRenderer = bool (UiRenderer::*)(RenderControlContext&);

    struct Event {
        JSContext* context;
        JSValue callback;
        JSValue argument;
    };

    struct Rect {
        float x = 0;
        float y = 0;
        float width = 0;
        float height = 0;
    };

    void collect_node(Session& session, JSValueConst node, std::vector<HostWindowRequest>& out);
    bool render_named_node(Session& session, JSValueConst node, const std::string& key);
    void render_window_contents(Session& session, JSValueConst node, const std::string& path);
    void render_node(Session& session, JSValueConst node, int depth, const std::string& path);
    void render_children(Session& session, JSValueConst children, int depth, const std::string& path);
    static ControlRenderer control_renderer(const std::string& type);
    bool render_window_control(RenderControlContext& control);
    bool render_column_control(RenderControlContext& control);
    bool render_page_control(RenderControlContext& control);
    bool render_row_control(RenderControlContext& control);
    bool render_container_control(RenderControlContext& control);
    bool render_modal_control(RenderControlContext& control);
    bool render_graphics_control(RenderControlContext& control);
    bool render_text_control(RenderControlContext& control);
    bool render_tree_indent_control(RenderControlContext& control);
    bool render_type_badge_control(RenderControlContext& control);
    bool render_disclosure_control(RenderControlContext& control);
    bool render_icon_button_control(RenderControlContext& control);
    bool render_button_control(RenderControlContext& control);
    bool render_checkbox_control(RenderControlContext& control);
    bool render_input_control(RenderControlContext& control);
    bool render_slider_control(RenderControlContext& control);
    bool render_separator_control(RenderControlContext& control);
    bool render_select_control(RenderControlContext& control);
    bool render_radio_control(RenderControlContext& control);
    bool render_tabs_control(RenderControlContext& control);
    bool render_list_control(RenderControlContext& control);
    bool render_scroll_control(RenderControlContext& control);
    bool render_progress_control(RenderControlContext& control);
    bool render_text_area_control(RenderControlContext& control);
    bool render_number_input_control(RenderControlContext& control);
    bool render_image_control(RenderControlContext& control);
#ifdef _WIN32
    void* load_win32_image(const std::string& source);
#endif
    bool render_tree_control(RenderControlContext& control);
    void draw_menu_bar(Session& session, JSValueConst menu);
    void draw_menu_items(Session& session, JSValueConst items, int depth);
    void queue_event(JSContext* context, JSValueConst callback, JSValue argument);
    void dispatch_events(Session& session);

    HostWindow* current_host_ = nullptr;

    std::vector<Event> events_;
    std::unordered_map<std::string, std::string> input_values_;
    std::unordered_map<std::string, bool> popup_open_;
#ifdef _WIN32
    std::unordered_map<std::string, void*> image_textures_;
#else
    std::unordered_map<std::string, SDL_Texture*> image_textures_;
#endif
    std::unordered_map<std::string, Rect> rects_;
    int node_count_ = 0;
    bool skip_window_nodes_ = false;
    std::string package_name_;
    std::string version_;
    std::string repository_;
};

} // namespace ykm
