#include "sys_api.h"

#include "host_tools.h"
#include "session.h"
#include "window_events.h"

namespace ykm {
namespace {
bool install(Session& session) {
    JSContext* context = session.context();
    JSValue global = JS_GetGlobalObject(context);
    JSValue api = JS_GetPropertyStr(context, global, "ykm");
    host_tools_install(context, api);
    JS_FreeValue(context, api);
    JS_FreeValue(context, global);
    return window_events_install(session);
}
const ApiUnit kUnit{"sys", install};
}

const ApiUnit& sys_api_unit() { return kUnit; }
} // namespace ykm
