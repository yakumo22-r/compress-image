#include "ui_imgui_api.h"

#include "component_api.h"
#include "fonts.h"
#include "session.h"

namespace ykm {
namespace {

bool install(Session& session) {
    JSContext* context = session.context();
    JSValue global = JS_GetGlobalObject(context);
    JSValue api = JS_GetPropertyStr(context, global, "ykm");
    fonts_install_js(context, api);
    JS_FreeValue(context, api);
    JS_FreeValue(context, global);
    return component_api_install(context);
}

const ApiUnit kUnit{"ui-imgui", install};

} // namespace

const ApiUnit& ui_imgui_api_unit() {
    return kUnit;
}

} // namespace ykm
