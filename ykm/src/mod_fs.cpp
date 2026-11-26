#include "event_loop.h"
#include "app_storage.h"
#include "jsapi.h"
#include "js_util.h"
#include "session.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace ykm {
namespace {

namespace fs = std::filesystem;

struct ResolvedPath {
    fs::path disk;
    std::string asset;
    bool is_asset = false;
};

std::string lower_copy(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool encoding_is_utf8(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 2 || JS_IsUndefined(argv[1]) || JS_IsNull(argv[1])) return false;
    if (JS_IsString(argv[1])) {
        std::string encoding = js_to_string(context, argv[1]);
        encoding = lower_copy(encoding);
        return encoding == "utf8" || encoding == "utf-8";
    }
    if (JS_IsObject(argv[1])) {
        JSValue encoding = JS_GetPropertyStr(context, argv[1], "encoding");
        std::string text = js_to_string(context, encoding);
        JS_FreeValue(context, encoding);
        text = lower_copy(text);
        return text == "utf8" || text == "utf-8";
    }
    return false;
}

bool option_flag(JSContext* context, int argc, JSValueConst* argv, const char* name) {
    if (argc < 2 || !JS_IsObject(argv[1])) return false;
    JSValue value = JS_GetPropertyStr(context, argv[1], name);
    const int result = JS_ToBool(context, value);
    JS_FreeValue(context, value);
    return result > 0;
}

JSValue throw_fs(JSContext* context, const char* code, const char* syscall, const std::string& path, const std::string& message) {
    JSValue error = JS_NewError(context);
    const std::string text = std::string(code) + ": " + message + ", " + syscall + " '" + path + "'";
    JS_DefinePropertyValueStr(context, error, "message", JS_NewString(context, text.c_str()), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_SetPropertyStr(context, error, "code", JS_NewString(context, code));
    JS_SetPropertyStr(context, error, "syscall", JS_NewString(context, syscall));
    JS_SetPropertyStr(context, error, "path", JS_NewString(context, path.c_str()));
    return JS_Throw(context, error);
}

bool is_within_root(const fs::path& root, const fs::path& candidate) {
    std::string root_text = root.lexically_normal().generic_string();
    std::string path_text = candidate.lexically_normal().generic_string();
#ifdef _WIN32
    root_text = lower_copy(root_text);
    path_text = lower_copy(path_text);
#endif
    if (path_text == root_text) return true;
    if (!root_text.empty() && root_text.back() != '/') root_text.push_back('/');
    return path_text.size() >= root_text.size() && path_text.compare(0, root_text.size(), root_text) == 0;
}

ResolvedPath sandbox_path(JSContext* context, JSValueConst value, const char* syscall, bool write, bool* ok) {
    *ok = false;
    auto* session = Session::from_context(context);
    if (!session) {
        JS_ThrowInternalError(context, "session is unavailable");
        return {};
    }
    const std::string requested = js_to_string(context, value);
    if (requested.empty()) {
        throw_fs(context, "EINVAL", syscall, requested, "invalid path");
        return {};
    }
    if (requested.rfind("asset:/", 0) == 0) {
        if (!app_allows("fs.asset")) {
            throw_fs(context, "EACCES", syscall, requested, "permission denied");
            return {};
        }
        if (write) {
            throw_fs(context, "EROFS", syscall, requested, "asset storage is read-only");
            return {};
        }
        const std::string asset = fs::path(requested.substr(7)).lexically_normal().generic_string();
        if (asset == ".." || asset.rfind("../", 0) == 0) {
            throw_fs(context, "EACCES", syscall, requested, "asset path escapes root");
            return {};
        }
        *ok = true;
        return {{}, asset == "." ? "" : asset, true};
    }

    fs::path root;
    std::string relative = requested;
    const char* permission = "fs.app";
    if (requested.rfind("data:/", 0) == 0) {
        permission = "fs.data";
        root = app_data_root();
        relative = requested.substr(6);
    } else if (requested.rfind("cache:/", 0) == 0) {
        permission = "fs.cache";
        root = app_cache_root();
        relative = requested.substr(7);
    } else {
        root = fs::absolute(session->root()).lexically_normal();
    }
    if (!app_allows(permission)) {
        throw_fs(context, "EACCES", syscall, requested, "permission denied");
        return {};
    }
    root = fs::absolute(root).lexically_normal();
    const fs::path input = fs::u8path(relative);
    const fs::path combined = (input.is_absolute() ? input : (root / input)).lexically_normal();
    if (!is_within_root(root, combined)) {
        throw_fs(context, "EACCES", syscall, requested, "path escapes application root");
        return {};
    }
    if ((requested.rfind("data:/", 0) == 0 || requested.rfind("cache:/", 0) == 0)) {
        std::error_code ec;
        fs::create_directories(root, ec);
    }
    *ok = true;
    return {combined, {}, false};
}

JSValue complete_promise(JSContext* context, JSValue funcs[2], JSValue result) {
    auto* session = Session::from_context(context);
    if (!session) {
        JS_FreeValue(context, funcs[0]);
        JS_FreeValue(context, funcs[1]);
        JS_FreeValue(context, result);
        return JS_ThrowInternalError(context, "session is unavailable");
    }
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(context);
        session->loop().enqueue_call(funcs[1], exception);
        JS_FreeValue(context, funcs[0]);
    } else {
        session->loop().enqueue_call(funcs[0], result);
        JS_FreeValue(context, funcs[1]);
    }
    return JS_UNDEFINED;
}

JSValue wrap_promise(JSContext* context, JSValue result) {
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(context, funcs);
    if (JS_IsException(promise)) {
        JS_FreeValue(context, result);
        return promise;
    }
    complete_promise(context, funcs, result);
    return promise;
}

JSValue read_file(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "readFile requires a path");
    bool ok = false;
    const ResolvedPath resolved = sandbox_path(context, argv[0], "open", false, &ok);
    if (!ok) return JS_EXCEPTION;
    if (resolved.is_asset) {
        AssetView asset;
        if (embedded_asset(resolved.asset, asset)) {
            if (encoding_is_utf8(context, argc, argv)) {
                return JS_NewStringLen(context, reinterpret_cast<const char*>(asset.data), asset.size);
            }
            return js_new_uint8_array(context, asset.data, asset.size);
        }
        auto* session = Session::from_context(context);
        const fs::path disk = session->root() / "assets" / resolved.asset;
        std::ifstream input(disk, std::ios::binary);
        if (!input) return throw_fs(context, "ENOENT", "open", "asset:/" + resolved.asset, "no such asset");
        std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (encoding_is_utf8(context, argc, argv)) return JS_NewStringLen(context, data.data(), data.size());
        return js_new_uint8_array(context, reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }
    const fs::path& path = resolved.disk;
    std::ifstream input(path, std::ios::binary);
    if (!input) return throw_fs(context, "ENOENT", "open", path.string(), "no such file or directory");
    std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (encoding_is_utf8(context, argc, argv)) return JS_NewStringLen(context, data.data(), data.size());
    return js_new_uint8_array(context, reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

JSValue write_file(JSContext* context, int argc, JSValueConst* argv, bool append) {
    if (argc < 2) return JS_ThrowTypeError(context, "writeFile requires a path and data");
    bool ok = false;
    const ResolvedPath resolved = sandbox_path(context, argv[0], "open", true, &ok);
    if (!ok) return JS_EXCEPTION;
    const fs::path& path = resolved.disk;
    std::string data;
    if (!js_copy_bytes(context, argv[1], data)) {
        return JS_ThrowTypeError(context, "data must be a string or Uint8Array");
    }
    std::ios::openmode mode = std::ios::binary;
    mode |= append ? std::ios::app : std::ios::trunc;
    std::ofstream output(path, mode);
    if (!output) return throw_fs(context, "ENOENT", "open", path.string(), "unable to write file");
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    return JS_UNDEFINED;
}

JSValue mkdir_path(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "mkdir requires a path");
    bool ok = false;
    const ResolvedPath resolved = sandbox_path(context, argv[0], "mkdir", true, &ok);
    if (!ok) return JS_EXCEPTION;
    const fs::path& path = resolved.disk;
    std::error_code ec;
    if (option_flag(context, argc, argv, "recursive")) {
        fs::create_directories(path, ec);
    } else {
        fs::create_directory(path, ec);
    }
    if (ec) return throw_fs(context, "EEXIST", "mkdir", path.string(), ec.message());
    return JS_UNDEFINED;
}

JSValue readdir_path(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "readdir requires a path");
    bool ok = false;
    const ResolvedPath resolved = sandbox_path(context, argv[0], "scandir", false, &ok);
    if (!ok) return JS_EXCEPTION;
    if (resolved.is_asset) {
        std::vector<std::string> names = embedded_asset_directory(resolved.asset);
        if (names.empty()) {
            auto* session = Session::from_context(context);
            const fs::path disk = session->root() / "assets" / resolved.asset;
            std::error_code ec;
            if (fs::is_directory(disk, ec)) {
                for (const auto& entry : fs::directory_iterator(disk, ec)) names.push_back(entry.path().filename().string());
            }
        }
        JSValue result = JS_NewArray(context);
        for (uint32_t index = 0; index < names.size(); ++index) {
            JS_SetPropertyUint32(context, result, index, JS_NewString(context, names[index].c_str()));
        }
        return result;
    }
    const fs::path& path = resolved.disk;
    std::error_code ec;
    if (!fs::is_directory(path, ec)) return throw_fs(context, "ENOTDIR", "scandir", path.string(), "not a directory");
    JSValue result = JS_NewArray(context);
    uint32_t index = 0;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        const auto utf8_name = entry.path().filename().u8string();
        const std::string name(utf8_name.begin(), utf8_name.end());
        JS_SetPropertyUint32(context, result, index++, JS_NewString(context, name.c_str()));
    }
    if (ec) {
        JS_FreeValue(context, result);
        return throw_fs(context, "EIO", "scandir", path.string(), ec.message());
    }
    return result;
}

JSValue js_stat_flag(JSContext* context, JSValueConst this_value, int, JSValueConst*, int magic) {
    JSValue value = JS_GetPropertyStr(context, this_value, magic ? "_dir" : "_file");
    const int result = JS_ToBool(context, value);
    JS_FreeValue(context, value);
    return JS_NewBool(context, result > 0);
}

JSValue stat_path(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "stat requires a path");
    bool ok = false;
    const ResolvedPath resolved = sandbox_path(context, argv[0], "stat", false, &ok);
    if (!ok) return JS_EXCEPTION;
    if (resolved.is_asset) {
        AssetView asset;
        const bool file = embedded_asset(resolved.asset, asset);
        const bool directory = !file && !embedded_asset_directory(resolved.asset).empty();
        if (file || directory) {
            JSValue stats = JS_NewObject(context);
            JS_SetPropertyStr(context, stats, "_file", JS_NewBool(context, file));
            JS_SetPropertyStr(context, stats, "_dir", JS_NewBool(context, directory));
            JS_SetPropertyStr(context, stats, "isFile", JS_NewCFunctionMagic(context, js_stat_flag, "isFile", 0, JS_CFUNC_generic_magic, 0));
            JS_SetPropertyStr(context, stats, "isDirectory", JS_NewCFunctionMagic(context, js_stat_flag, "isDirectory", 0, JS_CFUNC_generic_magic, 1));
            JS_SetPropertyStr(context, stats, "size", JS_NewInt64(context, file ? asset.size : 0));
            JS_SetPropertyStr(context, stats, "mtimeMs", JS_NewFloat64(context, 0));
            return stats;
        }
        auto* session = Session::from_context(context);
        const fs::path disk = session->root() / "assets" / resolved.asset;
        std::error_code disk_error;
        const auto disk_status = fs::status(disk, disk_error);
        if (!disk_error && disk_status.type() != fs::file_type::not_found) {
            JSValue stats = JS_NewObject(context);
            const bool disk_file = fs::is_regular_file(disk_status);
            const bool disk_directory = fs::is_directory(disk_status);
            JS_SetPropertyStr(context, stats, "_file", JS_NewBool(context, disk_file));
            JS_SetPropertyStr(context, stats, "_dir", JS_NewBool(context, disk_directory));
            JS_SetPropertyStr(context, stats, "isFile", JS_NewCFunctionMagic(context, js_stat_flag, "isFile", 0, JS_CFUNC_generic_magic, 0));
            JS_SetPropertyStr(context, stats, "isDirectory", JS_NewCFunctionMagic(context, js_stat_flag, "isDirectory", 0, JS_CFUNC_generic_magic, 1));
            const uintmax_t disk_size = disk_file ? fs::file_size(disk, disk_error) : 0;
            JS_SetPropertyStr(context, stats, "size", JS_NewInt64(context, static_cast<int64_t>(disk_size)));
            JS_SetPropertyStr(context, stats, "mtimeMs", JS_NewFloat64(context, 0));
            return stats;
        }
        return throw_fs(context, "ENOENT", "stat", "asset:/" + resolved.asset, "no such asset");
    }
    const fs::path& path = resolved.disk;
    std::error_code ec;
    const auto status = fs::status(path, ec);
    if (ec || status.type() == fs::file_type::not_found) {
        return throw_fs(context, "ENOENT", "stat", path.string(), "no such file or directory");
    }
    JSValue stats = JS_NewObject(context);
    const bool is_file = fs::is_regular_file(status);
    const bool is_dir = fs::is_directory(status);
    JS_SetPropertyStr(context, stats, "_file", JS_NewBool(context, is_file));
    JS_SetPropertyStr(context, stats, "_dir", JS_NewBool(context, is_dir));
    JS_SetPropertyStr(context, stats, "isFile", JS_NewCFunctionMagic(context, js_stat_flag, "isFile", 0, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(context, stats, "isDirectory", JS_NewCFunctionMagic(context, js_stat_flag, "isDirectory", 0, JS_CFUNC_generic_magic, 1));
    uintmax_t size = 0;
    if (is_file) size = fs::file_size(path, ec);
    JS_SetPropertyStr(context, stats, "size", JS_NewInt64(context, static_cast<int64_t>(size)));
    double mtime_ms = 0;
    const auto ftime = fs::last_write_time(path, ec);
    if (!ec) {
        const auto sys = std::chrono::system_clock::now() + (ftime - fs::file_time_type::clock::now());
        mtime_ms = std::chrono::duration<double, std::milli>(sys.time_since_epoch()).count();
    }
    JS_SetPropertyStr(context, stats, "mtimeMs", JS_NewFloat64(context, mtime_ms));
    return stats;
}

JSValue unlink_path(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "unlink requires a path");
    bool ok = false;
    const ResolvedPath resolved = sandbox_path(context, argv[0], "unlink", true, &ok);
    if (!ok) return JS_EXCEPTION;
    const fs::path& path = resolved.disk;
    std::error_code ec;
    if (!fs::remove(path, ec) || ec) return throw_fs(context, "ENOENT", "unlink", path.string(), "no such file or directory");
    return JS_UNDEFINED;
}

JSValue rm_path(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "rm requires a path");
    bool ok = false;
    const ResolvedPath resolved = sandbox_path(context, argv[0], "rm", true, &ok);
    if (!ok) return JS_EXCEPTION;
    const fs::path& path = resolved.disk;
    std::error_code ec;
    if (option_flag(context, argc, argv, "recursive")) {
        fs::remove_all(path, ec);
    } else {
        fs::remove(path, ec);
    }
    if (ec && !option_flag(context, argc, argv, "force")) {
        return throw_fs(context, "ENOENT", "rm", path.string(), ec.message());
    }
    return JS_UNDEFINED;
}

JSValue exists_path(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "existsSync requires a path");
    bool ok = false;
    const ResolvedPath resolved = sandbox_path(context, argv[0], "access", false, &ok);
    if (!ok) {
        JS_FreeValue(context, JS_GetException(context));
        return JS_FALSE;
    }
    if (resolved.is_asset) {
        if (embedded_asset_exists(resolved.asset)) return JS_TRUE;
        auto* session = Session::from_context(context);
        std::error_code ec;
        return JS_NewBool(context, fs::exists(session->root() / "assets" / resolved.asset, ec));
    }
    const fs::path& path = resolved.disk;
    std::error_code ec;
    return JS_NewBool(context, fs::exists(path, ec));
}

JSValue access_path(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "access requires a path");
    bool ok = false;
    const ResolvedPath resolved = sandbox_path(context, argv[0], "access", false, &ok);
    if (!ok) return JS_EXCEPTION;
    if (resolved.is_asset) {
        if (embedded_asset_exists(resolved.asset)) return JS_UNDEFINED;
        auto* session = Session::from_context(context);
        std::error_code ec;
        if (fs::exists(session->root() / "assets" / resolved.asset, ec)) return JS_UNDEFINED;
        return throw_fs(context, "ENOENT", "access", "asset:/" + resolved.asset, "no such asset");
    }
    const fs::path& path = resolved.disk;
    std::error_code ec;
    if (!fs::exists(path, ec)) return throw_fs(context, "ENOENT", "access", path.string(), "no such file or directory");
    return JS_UNDEFINED;
}

#define FS_SYNC(name, fn) \
    static JSValue js_##name##_sync(JSContext* context, JSValueConst, int argc, JSValueConst* argv) { return fn(context, argc, argv); }
#define FS_ASYNC(name, fn) \
    static JSValue js_##name(JSContext* context, JSValueConst, int argc, JSValueConst* argv) { return wrap_promise(context, fn(context, argc, argv)); }

FS_SYNC(read_file, read_file)
FS_SYNC(mkdir, mkdir_path)
FS_SYNC(readdir, readdir_path)
FS_SYNC(stat, stat_path)
FS_SYNC(unlink, unlink_path)
FS_SYNC(rm, rm_path)
FS_SYNC(exists, exists_path)
FS_SYNC(access, access_path)

static JSValue js_write_file_sync(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    return write_file(context, argc, argv, false);
}
static JSValue js_append_file_sync(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    return write_file(context, argc, argv, true);
}

FS_ASYNC(read_file, read_file)
FS_ASYNC(mkdir, mkdir_path)
FS_ASYNC(readdir, readdir_path)
FS_ASYNC(stat, stat_path)
FS_ASYNC(unlink, unlink_path)
FS_ASYNC(rm, rm_path)
FS_ASYNC(access, access_path)

static JSValue js_write_file(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    return wrap_promise(context, write_file(context, argc, argv, false));
}
static JSValue js_append_file(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    return wrap_promise(context, write_file(context, argc, argv, true));
}

void fill_fs_methods(JSContext* context, JSValue module, bool promise_api) {
    if (promise_api) {
        JS_SetPropertyStr(context, module, "readFile", JS_NewCFunction(context, js_read_file, "readFile", 2));
        JS_SetPropertyStr(context, module, "writeFile", JS_NewCFunction(context, js_write_file, "writeFile", 2));
        JS_SetPropertyStr(context, module, "appendFile", JS_NewCFunction(context, js_append_file, "appendFile", 2));
        JS_SetPropertyStr(context, module, "mkdir", JS_NewCFunction(context, js_mkdir, "mkdir", 1));
        JS_SetPropertyStr(context, module, "readdir", JS_NewCFunction(context, js_readdir, "readdir", 1));
        JS_SetPropertyStr(context, module, "stat", JS_NewCFunction(context, js_stat, "stat", 1));
        JS_SetPropertyStr(context, module, "unlink", JS_NewCFunction(context, js_unlink, "unlink", 1));
        JS_SetPropertyStr(context, module, "rm", JS_NewCFunction(context, js_rm, "rm", 1));
        JS_SetPropertyStr(context, module, "access", JS_NewCFunction(context, js_access, "access", 1));
        return;
    }
    JS_SetPropertyStr(context, module, "readFileSync", JS_NewCFunction(context, js_read_file_sync, "readFileSync", 2));
    JS_SetPropertyStr(context, module, "writeFileSync", JS_NewCFunction(context, js_write_file_sync, "writeFileSync", 2));
    JS_SetPropertyStr(context, module, "appendFileSync", JS_NewCFunction(context, js_append_file_sync, "appendFileSync", 2));
    JS_SetPropertyStr(context, module, "mkdirSync", JS_NewCFunction(context, js_mkdir_sync, "mkdirSync", 1));
    JS_SetPropertyStr(context, module, "readdirSync", JS_NewCFunction(context, js_readdir_sync, "readdirSync", 1));
    JS_SetPropertyStr(context, module, "statSync", JS_NewCFunction(context, js_stat_sync, "statSync", 1));
    JS_SetPropertyStr(context, module, "unlinkSync", JS_NewCFunction(context, js_unlink_sync, "unlinkSync", 1));
    JS_SetPropertyStr(context, module, "rmSync", JS_NewCFunction(context, js_rm_sync, "rmSync", 1));
    JS_SetPropertyStr(context, module, "existsSync", JS_NewCFunction(context, js_exists_sync, "existsSync", 1));
    JS_SetPropertyStr(context, module, "accessSync", JS_NewCFunction(context, js_access_sync, "accessSync", 1));
    JS_SetPropertyStr(context, module, "readFile", JS_NewCFunction(context, js_read_file, "readFile", 2));
    JS_SetPropertyStr(context, module, "writeFile", JS_NewCFunction(context, js_write_file, "writeFile", 2));
    JS_SetPropertyStr(context, module, "appendFile", JS_NewCFunction(context, js_append_file, "appendFile", 2));
    JS_SetPropertyStr(context, module, "mkdir", JS_NewCFunction(context, js_mkdir, "mkdir", 1));
    JS_SetPropertyStr(context, module, "readdir", JS_NewCFunction(context, js_readdir, "readdir", 1));
    JS_SetPropertyStr(context, module, "stat", JS_NewCFunction(context, js_stat, "stat", 1));
    JS_SetPropertyStr(context, module, "unlink", JS_NewCFunction(context, js_unlink, "unlink", 1));
    JS_SetPropertyStr(context, module, "rm", JS_NewCFunction(context, js_rm, "rm", 1));
    JS_SetPropertyStr(context, module, "access", JS_NewCFunction(context, js_access, "access", 1));
}

} // namespace

JSValue mod_fs_promises_open(JSContext* context) {
    JSValue module = JS_NewObject(context);
    fill_fs_methods(context, module, true);
    return module;
}

JSValue mod_fs_open(JSContext* context) {
    JSValue module = JS_NewObject(context);
    fill_fs_methods(context, module, false);
    JSValue promises = jsapi_require(context, "fs/promises");
    if (JS_IsException(promises)) {
        JS_FreeValue(context, module);
        return promises;
    }
    JS_SetPropertyStr(context, module, "promises", promises);
    return module;
}

} // namespace ykm
