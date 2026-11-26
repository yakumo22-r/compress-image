#pragma once

#include <string>
#include <vector>

extern "C" {
#include <quickjs.h>
}

struct ImFont;

namespace ykm {

bool fonts_add(const std::string& name, const std::string& path, float size);
ImFont* fonts_get(const std::string& name);
std::vector<std::string> fonts_list();
void fonts_flush();
void fonts_load_ui_default();
void ui_zoom_capture_base();
void ui_zoom_sync(float px_scale, bool rebuild_font = true);
void fonts_install_js(JSContext* context, JSValue api);

} // namespace ykm
