#include "component_api.h"

#include "window_layer.h"
#include "component_relations.h"
#include <string>

namespace ykm {
namespace {

constexpr const char* kComponentApi =
#include "component_api.inc"
;

} // namespace

bool component_api_install(JSContext* context) {
    JSValue global = JS_GetGlobalObject(context);
    JSValue api = JS_GetPropertyStr(context, global, "ykm");
    JSValue relation = JS_NewObject(context);
#define YKM_RELATION(name) \
    JS_SetPropertyStr(context, relation, #name, JS_NewInt32(context, static_cast<int32_t>(RelationType::name)))
    YKM_RELATION(Width);
    YKM_RELATION(Height);
    YKM_RELATION(Size);
    YKM_RELATION(LeftLeft);
    YKM_RELATION(LeftCenter);
    YKM_RELATION(LeftRight);
    YKM_RELATION(CenterCenter);
    YKM_RELATION(RightLeft);
    YKM_RELATION(RightRight);
    YKM_RELATION(TopTop);
    YKM_RELATION(TopMiddle);
    YKM_RELATION(TopBottom);
    YKM_RELATION(MiddleMiddle);
    YKM_RELATION(BottomTop);
    YKM_RELATION(BottomBottom);
#undef YKM_RELATION
    JS_SetPropertyStr(context, api, "Relation", relation);

    JSValue layer = JS_NewObject(context);
#define YKM_LAYER(name) \
    JS_SetPropertyStr(context, layer, #name, JS_NewInt32(context, static_cast<int32_t>(WindowLayer::name)))
    YKM_LAYER(BG);
    YKM_LAYER(Page);
    YKM_LAYER(Modal);
    YKM_LAYER(System);
    YKM_LAYER(Debug);
#undef YKM_LAYER
    JS_SetPropertyStr(context, api, "Layer", layer);

    JS_FreeValue(context, api);
    JS_FreeValue(context, global);

    JSValue result = JS_Eval(context, kComponentApi, std::char_traits<char>::length(kComponentApi),
                             "<ykm-component-api>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JS_FreeValue(context, result);
        return false;
    }
    JS_FreeValue(context, result);
    return true;
}

} // namespace ykm
