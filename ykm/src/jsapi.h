#pragma once

#include <ykm/jsapi.h>

#include <string>
#include <unordered_map>

namespace ykm {

struct JsApi {
    const char* name;
    JSValue (*open)(JSContext* context);
    const char* const* export_names = nullptr;
    uint32_t export_count = 0;
};

void jsapi_register(const JsApi& api);
void jsapi_register_builtins();
void jsapi_install_require(JSContext* context);
JSValue jsapi_require(JSContext* context, const char* name);
bool jsapi_module_source(const char* name, std::string& source);
void jsapi_free_modules(std::unordered_map<std::string, JSValue>& modules, JSContext* context);

JSValue mod_path_open(JSContext* context);
JSValue mod_path_win32_open(JSContext* context);
JSValue mod_path_posix_open(JSContext* context);
JSValue mod_fs_open(JSContext* context);
JSValue mod_fs_promises_open(JSContext* context);
JSValue mod_webp_open(JSContext* context);

} // namespace ykm
