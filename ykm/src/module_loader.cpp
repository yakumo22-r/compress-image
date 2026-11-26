#include "module_loader.h"

#include "jsapi.h"

#include <ykm/embedded_app.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

namespace ykm {
namespace {

std::string lower_copy(std::string value) {
#ifdef _WIN32
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return value;
}

bool within_root(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::string root_text = lower_copy(root.lexically_normal().generic_string());
    const std::string path_text = lower_copy(path.lexically_normal().generic_string());
    if (path_text == root_text) return true;
    if (!root_text.empty() && root_text.back() != '/') root_text.push_back('/');
    return path_text.size() >= root_text.size() && path_text.compare(0, root_text.size(), root_text) == 0;
}

JSModuleDef* compile_module(JSContext* context, const std::string& source, const char* name) {
    JSValue compiled = JS_Eval(context, source.data(), source.size(), name,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) return nullptr;
    return static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
}

} // namespace

void ModuleLoader::install(JSRuntime* runtime, std::filesystem::path root) {
    root_ = std::filesystem::absolute(std::move(root)).lexically_normal();
    JS_SetModuleLoaderFunc(runtime, normalize, load, this);
}

bool ModuleLoader::embedded_source(const std::string& name, std::string& source) {
    const YkmEmbeddedApp* app = ykm_get_embedded_app();
    if (!app || app->struct_size < sizeof(YkmEmbeddedApp)) return false;
    for (uint32_t index = 0; index < app->module_count; ++index) {
        const YkmEmbeddedModule& module = app->modules[index];
        if (module.name && name == module.name) {
            source.assign(reinterpret_cast<const char*>(module.data), module.size);
            return true;
        }
    }
    return false;
}

char* ModuleLoader::normalize(JSContext* context, const char* base_name, const char* name, void* opaque) {
    auto* loader = static_cast<ModuleLoader*>(opaque);
    if (!loader || !name || !*name) {
        JS_ThrowReferenceError(context, "invalid module name");
        return nullptr;
    }

    const std::string_view requested(name);
    std::string normalized;
    if (requested.rfind("native:", 0) == 0) {
        normalized = std::string(requested);
    } else if (requested.rfind("app:/", 0) == 0) {
        normalized = std::string(requested);
    } else if (requested[0] != '.' && requested[0] != '/' &&
               !(requested.size() > 2 && std::isalpha(static_cast<unsigned char>(requested[0])) && requested[1] == ':')) {
        normalized = "native:" + std::string(requested);
    } else {
        if (base_name && std::string_view(base_name).rfind("app:/", 0) == 0) {
            std::filesystem::path base(std::string(base_name).substr(5));
            std::filesystem::path candidate = (base.parent_path() / std::string(requested)).lexically_normal();
            const std::string virtual_path = candidate.generic_string();
            if (virtual_path.rfind("../", 0) == 0 || virtual_path == "..") {
                JS_ThrowReferenceError(context, "module path escapes embedded application: %s", name);
                return nullptr;
            }
            normalized = "app:/" + virtual_path;
            char* result = static_cast<char*>(js_malloc(context, normalized.size() + 1));
            if (!result) return nullptr;
            std::memcpy(result, normalized.c_str(), normalized.size() + 1);
            return result;
        }
        std::filesystem::path candidate;
        if (requested[0] == '.') {
            const std::filesystem::path base(base_name && *base_name ? base_name : loader->root_.string());
            candidate = base.parent_path() / std::string(requested);
        } else {
            candidate = std::filesystem::path(std::string(requested));
        }
        candidate = std::filesystem::absolute(candidate).lexically_normal();
        if (!within_root(loader->root_, candidate)) {
            JS_ThrowReferenceError(context, "module path escapes application root: %s", name);
            return nullptr;
        }
        normalized = candidate.generic_string();
    }

    char* result = static_cast<char*>(js_malloc(context, normalized.size() + 1));
    if (!result) return nullptr;
    std::memcpy(result, normalized.c_str(), normalized.size() + 1);
    return result;
}

bool ModuleLoader::resolve_file(const std::string& name, std::filesystem::path& result) const {
    result = std::filesystem::path(name).lexically_normal();
    return within_root(root_, result) && std::filesystem::is_regular_file(result);
}

JSModuleDef* ModuleLoader::load(JSContext* context, const char* name, void* opaque) {
    auto* loader = static_cast<ModuleLoader*>(opaque);
    if (!loader || !name) return nullptr;
    const std::string module_name(name);
    if (module_name.rfind("native:", 0) == 0) {
        std::string source;
        if (!jsapi_module_source(module_name.substr(7).c_str(), source)) {
            JS_ThrowReferenceError(context, "Cannot find native module '%s'", module_name.substr(7).c_str());
            return nullptr;
        }
        return compile_module(context, source, name);
    }

    std::string embedded;
    if (embedded_source(module_name, embedded)) {
        return compile_module(context, embedded, name);
    }

    std::filesystem::path path;
    if (!loader->resolve_file(module_name, path)) {
        JS_ThrowReferenceError(context, "Cannot find module '%s'", name);
        return nullptr;
    }
    std::ifstream input(path, std::ios::binary);
    std::ostringstream stream;
    stream << input.rdbuf();
    return compile_module(context, stream.str(), name);
}

} // namespace ykm
