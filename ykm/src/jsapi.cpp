#include "jsapi.h"

#include "event_loop.h"
#include "session.h"

#include <mutex>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace ykm {
namespace {

std::mutex g_mutex;
std::vector<JsApi> g_apis;
std::once_flag g_builtins;

const char* strip_node_prefix(const char* name) {
    const std::string_view view(name);
    if (view.size() > 5 && view.substr(0, 5) == "node:") return name + 5;
    return name;
}

std::optional<JsApi> find_api(const char* name) {
    const char* key = strip_node_prefix(name);
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const JsApi& api : g_apis) {
        if (std::string_view(api.name) == key) return api;
    }
    return std::nullopt;
}

JSValue js_require(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "require(id) requires a module id");
    const char* name = JS_ToCString(context, argv[0]);
    if (!name) return JS_EXCEPTION;
    JSValue result = jsapi_require(context, name);
    JS_FreeCString(context, name);
    return result;
}

} // namespace

void jsapi_register(const JsApi& api) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (JsApi& existing : g_apis) {
        if (std::string_view(existing.name) == api.name) {
            existing = api;
            return;
        }
    }
    g_apis.push_back(api);
}

void jsapi_register_builtins() {
    std::call_once(g_builtins, [] {
    static const char* path_exports[] = {"sep", "delimiter", "win32", "posix", "join", "resolve", "normalize", "dirname", "basename", "extname", "isAbsolute", "relative", "parse", "format"};
    static const char* fs_exports[] = {"promises", "readFile", "writeFile", "appendFile", "mkdir", "readdir", "stat", "unlink", "rm", "access", "readFileSync", "writeFileSync", "appendFileSync", "mkdirSync", "readdirSync", "statSync", "unlinkSync", "rmSync", "existsSync", "accessSync"};
    static const char* fs_promises_exports[] = {"readFile", "writeFile", "appendFile", "mkdir", "readdir", "stat", "unlink", "rm", "access"};
    jsapi_register({"path", mod_path_open, path_exports, static_cast<uint32_t>(std::size(path_exports))});
    jsapi_register({"path/win32", mod_path_win32_open, path_exports, static_cast<uint32_t>(std::size(path_exports))});
    jsapi_register({"path/posix", mod_path_posix_open, path_exports, static_cast<uint32_t>(std::size(path_exports))});
    jsapi_register({"fs", mod_fs_open, fs_exports, static_cast<uint32_t>(std::size(fs_exports))});
    jsapi_register({"fs/promises", mod_fs_promises_open, fs_promises_exports, static_cast<uint32_t>(std::size(fs_promises_exports))});
    static const char* webp_exports[] = {"decode", "encode"};
    jsapi_register({"ykm:webp", mod_webp_open, webp_exports, static_cast<uint32_t>(std::size(webp_exports))});
    ykm_jsapi_register_packed();
    });
}

void jsapi_install_require(JSContext* context) {
    jsapi_register_builtins();
    JSValue global = JS_GetGlobalObject(context);
    JS_SetPropertyStr(context, global, "require", JS_NewCFunction(context, js_require, "require", 1));
    JS_FreeValue(context, global);
}

JSValue jsapi_require(JSContext* context, const char* name) {
    auto* session = Session::from_context(context);
    if (!session) return JS_ThrowInternalError(context, "session is unavailable");
    const char* key = strip_node_prefix(name);
    auto& modules = session->modules();
    auto found = modules.find(key);
    if (found != modules.end()) return JS_DupValue(context, found->second);

    const auto api = find_api(key);
    if (session->is_worker() && api && api->open != mod_path_open && api->open != mod_path_win32_open &&
        api->open != mod_path_posix_open && api->open != mod_fs_open && api->open != mod_fs_promises_open &&
        api->open != mod_webp_open)
        return JS_ThrowReferenceError(context, "module '%s' is unavailable in workers", name);
    if (!api || !api->open) {
        return JS_ThrowReferenceError(context, "Cannot find module '%s'", name);
    }
    JSValue exports = api->open(context);
    if (JS_IsException(exports)) return exports;
    modules.emplace(key, JS_DupValue(context, exports));
    return exports;
}

bool jsapi_module_source(const char* name, std::string& source) {
    jsapi_register_builtins();
    const auto api = find_api(name);
    if (!api) return false;
    source = "const module = globalThis.require(";
    source += '"';
    source += name;
    source += "\"); export default module;";
    for (uint32_t index = 0; index < api->export_count; ++index) {
        const char* export_name = api->export_names[index];
        if (!export_name || !*export_name) continue;
        source += " export const ";
        source += export_name;
        source += " = module.";
        source += export_name;
        source += ';';
    }
    return true;
}

void jsapi_free_modules(std::unordered_map<std::string, JSValue>& modules, JSContext* context) {
    for (auto& entry : modules) {
        JS_FreeValue(context, entry.second);
    }
    modules.clear();
}

} // namespace ykm

extern "C" YkmJsApiResult ykm_jsapi_register(const YkmJsApi* api) {
    if (!api || !api->name || !api->open) return YKM_JSAPI_INVALID_ARGUMENT;
    if (api->struct_size < sizeof(YkmJsApi) || api->abi_version != YKM_JSAPI_ABI_VERSION) {
        return YKM_JSAPI_INCOMPATIBLE_ABI;
    }
    ykm::jsapi_register({api->name, api->open, api->export_names, api->export_count});
    return YKM_JSAPI_OK;
}
