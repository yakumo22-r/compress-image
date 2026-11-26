#include "jsapi.h"
#include "js_util.h"
#include "session.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace ykm {
namespace {

enum class PathKind { Win, Posix };

char path_sep(PathKind kind) {
    return kind == PathKind::Win ? '\\' : '/';
}

bool is_sep(char c, PathKind kind) {
    if (kind == PathKind::Win) return c == '/' || c == '\\';
    return c == '/';
}

bool is_absolute(std::string_view path, PathKind kind) {
    if (path.empty()) return false;
    if (kind == PathKind::Win) {
        if (is_sep(path[0], kind)) return true;
        return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
    }
    return path[0] == '/';
}

std::string replace_seps(std::string_view path, PathKind kind) {
    std::string result(path);
    const char sep = path_sep(kind);
    for (char& c : result) {
        if (c == '/' || c == '\\') c = sep;
    }
    return result;
}

std::vector<std::string> split_parts(std::string_view path, PathKind kind, bool& absolute, std::string& root) {
    absolute = is_absolute(path, kind);
    root.clear();
    size_t index = 0;
    if (kind == PathKind::Win && path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        root = std::string(path.substr(0, 2));
        index = 2;
        if (index < path.size() && is_sep(path[index], kind)) {
            root += path_sep(kind);
            absolute = true;
            while (index < path.size() && is_sep(path[index], kind)) ++index;
        }
    } else if (!path.empty() && is_sep(path[0], kind)) {
        root = std::string(1, path_sep(kind));
        absolute = true;
        while (index < path.size() && is_sep(path[index], kind)) ++index;
        if (kind == PathKind::Win && index < path.size() && is_sep(path[0], kind) && is_sep(path.size() > 1 ? path[1] : '\0', kind)) {
            root = std::string(2, path_sep(kind));
        }
    }
    std::vector<std::string> parts;
    std::string current;
    for (; index < path.size(); ++index) {
        if (is_sep(path[index], kind)) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(path[index]);
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

std::string normalize_parts(const std::vector<std::string>& parts, bool absolute, const std::string& root, PathKind kind) {
    std::vector<std::string> stack;
    for (const std::string& part : parts) {
        if (part == "." ) continue;
        if (part == "..") {
            if (!stack.empty() && stack.back() != "..") {
                stack.pop_back();
            } else if (!absolute) {
                stack.emplace_back("..");
            }
            continue;
        }
        stack.push_back(part);
    }
    std::string result = root;
    const char sep = path_sep(kind);
    for (size_t i = 0; i < stack.size(); ++i) {
        if (i > 0 || (!root.empty() && root.back() != sep && root != ":")) result.push_back(sep);
        if (!root.empty() && root.back() == sep && i == 0) {
            result += stack[i];
        } else if (root.empty() || i > 0 || root.back() != sep) {
            if (!(i == 0 && !root.empty() && root.back() == sep)) {
                if (i > 0) {
                    result += stack[i];
                } else if (root.empty()) {
                    result += stack[i];
                } else {
                    result += stack[i];
                }
            } else {
                result += stack[i];
            }
        } else {
            result += stack[i];
        }
    }
    if (result.empty()) return ".";
    return result;
}

std::string normalize_path(std::string_view path, PathKind kind) {
    if (path.empty()) return ".";
    bool absolute = false;
    std::string root;
    auto parts = split_parts(path, kind, absolute, root);
    std::string result = normalize_parts(parts, absolute, root, kind);
    if (!path.empty() && is_sep(path.back(), kind) && result != "." && (result.empty() || !is_sep(result.back(), kind))) {
        result.push_back(path_sep(kind));
    }
    return result;
}

std::string join_path(const std::vector<std::string>& parts, PathKind kind) {
    std::string joined;
    for (const std::string& part : parts) {
        if (part.empty()) continue;
        if (joined.empty() || is_absolute(part, kind)) {
            joined = part;
            continue;
        }
        if (!is_sep(joined.back(), kind)) joined.push_back(path_sep(kind));
        joined += part;
    }
    if (joined.empty()) return ".";
    return normalize_path(joined, kind);
}

std::string dirname_path(std::string_view path, PathKind kind) {
    const std::string normalized = replace_seps(path, kind);
    if (normalized.empty()) return ".";
    size_t end = normalized.size();
    while (end > 1 && is_sep(normalized[end - 1], kind)) --end;
    const size_t slash = normalized.find_last_of(kind == PathKind::Win ? "/\\" : "/", end - 1);
    if (slash == std::string::npos) {
        if (kind == PathKind::Win && end >= 2 && normalized[1] == ':') return normalized.substr(0, 2);
        return ".";
    }
    if (slash == 0) return std::string(1, path_sep(kind));
    return normalized.substr(0, slash);
}

std::string basename_path(std::string_view path, std::string_view ext, PathKind kind) {
    const std::string normalized = replace_seps(path, kind);
    size_t end = normalized.size();
    while (end > 0 && is_sep(normalized[end - 1], kind)) --end;
    const size_t slash = normalized.find_last_of(kind == PathKind::Win ? "/\\" : "/", end == 0 ? 0 : end - 1);
    std::string base = slash == std::string::npos ? normalized.substr(0, end) : normalized.substr(slash + 1, end - slash - 1);
    if (!ext.empty() && ext.size() <= base.size() && base.compare(base.size() - ext.size(), ext.size(), ext) == 0) {
        base.resize(base.size() - ext.size());
    }
    return base;
}

std::string extname_path(std::string_view path, PathKind kind) {
    const std::string base = basename_path(path, {}, kind);
    if (base.empty() || base == "." || base == "..") return {};
    const size_t dot = base.rfind('.');
    if (dot == std::string::npos || dot == 0) return {};
    return base.substr(dot);
}

JSValue args_to_strings(JSContext* context, int argc, JSValueConst* argv, std::vector<std::string>& parts) {
    parts.reserve(static_cast<size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        parts.push_back(js_to_string(context, argv[index]));
        if (JS_IsException(JS_UNDEFINED)) {
        }
    }
    return JS_UNDEFINED;
}

JSValue js_join(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    std::vector<std::string> parts;
    args_to_strings(context, argc, argv, parts);
    return JS_NewString(context, join_path(parts, static_cast<PathKind>(magic)).c_str());
}

JSValue js_normalize(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    const std::string path = argc > 0 ? js_to_string(context, argv[0]) : ".";
    return JS_NewString(context, normalize_path(path, static_cast<PathKind>(magic)).c_str());
}

JSValue js_is_absolute(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    const std::string path = argc > 0 ? js_to_string(context, argv[0]) : "";
    return JS_NewBool(context, is_absolute(path, static_cast<PathKind>(magic)));
}

JSValue js_dirname(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    const std::string path = argc > 0 ? js_to_string(context, argv[0]) : "";
    return JS_NewString(context, dirname_path(path, static_cast<PathKind>(magic)).c_str());
}

JSValue js_basename(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    const std::string path = argc > 0 ? js_to_string(context, argv[0]) : "";
    const std::string ext = argc > 1 ? js_to_string(context, argv[1]) : "";
    return JS_NewString(context, basename_path(path, ext, static_cast<PathKind>(magic)).c_str());
}

JSValue js_extname(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    const std::string path = argc > 0 ? js_to_string(context, argv[0]) : "";
    return JS_NewString(context, extname_path(path, static_cast<PathKind>(magic)).c_str());
}

JSValue js_resolve(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    auto* session = Session::from_context(context);
    const PathKind kind = static_cast<PathKind>(magic);
    std::string result = session ? session->root().string() : ".";
    for (int index = 0; index < argc; ++index) {
        const std::string part = js_to_string(context, argv[index]);
        if (part.empty()) continue;
        if (is_absolute(part, kind)) result = part;
        else {
            if (!result.empty() && !is_sep(result.back(), kind)) result.push_back(path_sep(kind));
            result += part;
        }
    }
    return JS_NewString(context, normalize_path(result, kind).c_str());
}

JSValue js_relative(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    if (argc < 2) return JS_NewString(context, "");
    const PathKind kind = static_cast<PathKind>(magic);
    const std::string from = normalize_path(js_to_string(context, argv[0]), kind);
    const std::string to = normalize_path(js_to_string(context, argv[1]), kind);
    bool from_abs = false;
    bool to_abs = false;
    std::string from_root;
    std::string to_root;
    auto from_parts = split_parts(from, kind, from_abs, from_root);
    auto to_parts = split_parts(to, kind, to_abs, to_root);
    if (from_root != to_root) return JS_NewString(context, to.c_str());
    size_t index = 0;
    while (index < from_parts.size() && index < to_parts.size() && from_parts[index] == to_parts[index]) ++index;
    std::string result;
    for (size_t i = index; i < from_parts.size(); ++i) {
        if (!result.empty()) result.push_back(path_sep(kind));
        result += "..";
    }
    for (size_t i = index; i < to_parts.size(); ++i) {
        if (!result.empty()) result.push_back(path_sep(kind));
        result += to_parts[i];
    }
    if (result.empty()) result = ".";
    return JS_NewString(context, result.c_str());
}

JSValue js_parse(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    const PathKind kind = static_cast<PathKind>(magic);
    const std::string path = argc > 0 ? js_to_string(context, argv[0]) : "";
    JSValue object = JS_NewObject(context);
    bool absolute = false;
    std::string root;
    split_parts(path, kind, absolute, root);
    const std::string dir = dirname_path(path, kind);
    const std::string base = basename_path(path, {}, kind);
    const std::string ext = extname_path(path, kind);
    std::string name = base;
    if (!ext.empty() && name.size() >= ext.size()) name.resize(name.size() - ext.size());
    JS_SetPropertyStr(context, object, "root", JS_NewString(context, root.c_str()));
    JS_SetPropertyStr(context, object, "dir", JS_NewString(context, dir.c_str()));
    JS_SetPropertyStr(context, object, "base", JS_NewString(context, base.c_str()));
    JS_SetPropertyStr(context, object, "ext", JS_NewString(context, ext.c_str()));
    JS_SetPropertyStr(context, object, "name", JS_NewString(context, name.c_str()));
    return object;
}

JSValue js_format(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    if (argc < 1 || !JS_IsObject(argv[0])) return JS_NewString(context, "");
    const PathKind kind = static_cast<PathKind>(magic);
    JSValue dir_value = JS_GetPropertyStr(context, argv[0], "dir");
    JSValue base_value = JS_GetPropertyStr(context, argv[0], "base");
    JSValue name_value = JS_GetPropertyStr(context, argv[0], "name");
    JSValue ext_value = JS_GetPropertyStr(context, argv[0], "ext");
    std::string dir = js_to_string(context, dir_value);
    std::string base = js_to_string(context, base_value);
    if (base.empty()) base = js_to_string(context, name_value) + js_to_string(context, ext_value);
    JS_FreeValue(context, dir_value);
    JS_FreeValue(context, base_value);
    JS_FreeValue(context, name_value);
    JS_FreeValue(context, ext_value);
    if (dir.empty()) return JS_NewString(context, base.c_str());
    if (!is_sep(dir.back(), kind)) dir.push_back(path_sep(kind));
    dir += base;
    return JS_NewString(context, dir.c_str());
}

JSValue make_path_module(JSContext* context, PathKind kind) {
    const int magic = static_cast<int>(kind);
    JSValue module = JS_NewObject(context);
    const char sep[2] = {path_sep(kind), 0};
    JS_SetPropertyStr(context, module, "sep", JS_NewString(context, sep));
    JS_SetPropertyStr(context, module, "delimiter", JS_NewString(context, kind == PathKind::Win ? ";" : ":"));
    JS_SetPropertyStr(context, module, "join", JS_NewCFunctionMagic(context, js_join, "join", 0, JS_CFUNC_generic_magic, magic));
    JS_SetPropertyStr(context, module, "normalize", JS_NewCFunctionMagic(context, js_normalize, "normalize", 1, JS_CFUNC_generic_magic, magic));
    JS_SetPropertyStr(context, module, "isAbsolute", JS_NewCFunctionMagic(context, js_is_absolute, "isAbsolute", 1, JS_CFUNC_generic_magic, magic));
    JS_SetPropertyStr(context, module, "dirname", JS_NewCFunctionMagic(context, js_dirname, "dirname", 1, JS_CFUNC_generic_magic, magic));
    JS_SetPropertyStr(context, module, "basename", JS_NewCFunctionMagic(context, js_basename, "basename", 1, JS_CFUNC_generic_magic, magic));
    JS_SetPropertyStr(context, module, "extname", JS_NewCFunctionMagic(context, js_extname, "extname", 1, JS_CFUNC_generic_magic, magic));
    JS_SetPropertyStr(context, module, "resolve", JS_NewCFunctionMagic(context, js_resolve, "resolve", 0, JS_CFUNC_generic_magic, magic));
    JS_SetPropertyStr(context, module, "relative", JS_NewCFunctionMagic(context, js_relative, "relative", 2, JS_CFUNC_generic_magic, magic));
    JS_SetPropertyStr(context, module, "parse", JS_NewCFunctionMagic(context, js_parse, "parse", 1, JS_CFUNC_generic_magic, magic));
    JS_SetPropertyStr(context, module, "format", JS_NewCFunctionMagic(context, js_format, "format", 1, JS_CFUNC_generic_magic, magic));
    return module;
}

} // namespace

JSValue mod_path_win32_open(JSContext* context) {
    return make_path_module(context, PathKind::Win);
}

JSValue mod_path_posix_open(JSContext* context) {
    return make_path_module(context, PathKind::Posix);
}

JSValue mod_path_open(JSContext* context) {
#ifdef _WIN32
    JSValue module = jsapi_require(context, "path/win32");
#else
    JSValue module = jsapi_require(context, "path/posix");
#endif
    if (JS_IsException(module)) return module;
    JSValue win32 = jsapi_require(context, "path/win32");
    JSValue posix = jsapi_require(context, "path/posix");
    JS_SetPropertyStr(context, module, "win32", win32);
    JS_SetPropertyStr(context, module, "posix", posix);
    return module;
}

} // namespace ykm
