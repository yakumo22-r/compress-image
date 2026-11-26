#include "host_tools.h"
#include "app_storage.h"
#include "image_import.h"

#include "js_util.h"
#include "session.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#endif

namespace ykm {
namespace {

std::wstring wide(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring result(count > 0 ? static_cast<size_t>(count - 1) : 0, L'\0');
    if (count > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), count);
    return result;
#else
    return std::wstring(text.begin(), text.end());
#endif
}

std::string utf8(const std::wstring& text) {
#ifdef _WIN32
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(count > 0 ? static_cast<size_t>(count - 1) : 0, '\0');
    if (count > 1) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), count, nullptr, nullptr);
    return result;
#else
    return std::string(text.begin(), text.end());
#endif
}

std::filesystem::path find_sdk() {
    if (const char* env = std::getenv("YKM_SDK")) {
        std::filesystem::path root(env);
        if (std::filesystem::exists(root / "tools" / "ykm-app.mjs")) return root;
    }
    std::vector<std::filesystem::path> starts = {
        std::filesystem::current_path(),
    };
#ifdef _WIN32
    wchar_t module[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, module, MAX_PATH)) starts.emplace_back(std::filesystem::path(module).parent_path());
#endif
    for (std::filesystem::path path : starts) {
        for (int depth = 0; depth < 8; ++depth) {
            if (std::filesystem::exists(path / "tools" / "ykm-app.mjs")) return path;
            if (!path.has_parent_path() || path == path.parent_path()) break;
            path = path.parent_path();
        }
    }
    return {};
}

std::filesystem::path find_node() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    if (SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, buffer, nullptr)) return buffer;
#endif
    return {};
}

struct CommandResult {
    bool ok = false;
    std::string log;
};

CommandResult run_ykm(const std::string& verb, const std::string& project, const std::string& mode) {
    const std::filesystem::path sdk = find_sdk();
    const std::filesystem::path node = find_node();
    if (sdk.empty()) return {false, "YKM SDK not found (set YKM_SDK or run from the ykm-app tree)"};
    if (node.empty()) return {false, "node.exe not found on PATH"};
    const std::filesystem::path script = sdk / "tools" / "ykm-app.mjs";
#ifdef _WIN32
    std::wstring command = L"\"" + node.wstring() + L"\" \"" + script.wstring() + L"\" " + wide(verb) + L" \"" + wide(project) + L"\"";
    if (verb == "package" && !mode.empty()) command += L" --mode " + wide(mode);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(0);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE reader = nullptr;
    HANDLE writer = nullptr;
    if (!CreatePipe(&reader, &writer, &security, 0)) return {false, "failed to create pipe"};
    SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writer;
    startup.hStdError = writer;
    PROCESS_INFORMATION process{};
    std::wstring directory = sdk.wstring();
    const BOOL created = CreateProcessW(
        nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process);
    CloseHandle(writer);
    if (!created) {
        CloseHandle(reader);
        return {false, "failed to start node"};
    }
    std::string output;
    char chunk[4096];
    DWORD read = 0;
    while (ReadFile(reader, chunk, sizeof(chunk), &read, nullptr) && read > 0) output.append(chunk, chunk + read);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(reader);
    if (code != 0 && output.empty()) output = "command failed";
    return {code == 0, output};
#else
    return {false, "Windows only"};
#endif
}

std::string pick_directory() {
#ifdef _WIN32
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    std::string result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = utf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
#else
    return {};
#endif
}

#ifdef _WIN32
void set_json_filter(IFileDialog* dialog) {
    const COMDLG_FILTERSPEC filters[] = {
        {L"JSON 文件 (*.json)", L"*.json"},
        {L"所有文件 (*.*)", L"*.*"},
    };
    dialog->SetFileTypes(2, filters);
    dialog->SetFileTypeIndex(1);
}

std::string dialog_path(IFileDialog* dialog) {
    std::string result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = utf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    return result;
}
#endif

JSValue js_open_file(JSContext* context, JSValueConst, int, JSValueConst*) {
#ifdef _WIN32
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return JS_NULL;
    set_json_filter(dialog);
    const std::string path = dialog_path(dialog);
    dialog->Release();
    if (path.empty()) return JS_NULL;
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input) return JS_ThrowInternalError(context, "failed to open file");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    JSValue result = JS_NewObject(context);
    JS_SetPropertyStr(context, result, "path", JS_NewString(context, path.c_str()));
    const std::string content = buffer.str();
    JS_SetPropertyStr(context, result, "content", JS_NewStringLen(context, content.data(), content.size()));
    return result;
#else
    return JS_NULL;
#endif
}

JSValue js_save_file(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "saveFile requires content");
    const std::string content = js_to_string(context, argv[0]);
    std::string path = argc >= 2 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])
        ? js_to_string(context, argv[1]) : std::string{};
#ifdef _WIN32
    if (path.empty()) {
        IFileSaveDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return JS_NULL;
        set_json_filter(dialog);
        dialog->SetDefaultExtension(L"json");
        path = dialog_path(dialog);
        dialog->Release();
    }
#endif
    if (path.empty()) return JS_NULL;
    std::ofstream output(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!output || !(output << content)) return JS_ThrowInternalError(context, "failed to save file");
    return JS_NewString(context, path.c_str());
}

JSValue js_pick_directory(JSContext* context, JSValueConst, int, JSValueConst*) {
    const std::string path = pick_directory();
    if (path.empty()) return JS_NULL;
    return JS_NewString(context, path.c_str());
}

namespace fs = std::filesystem;
fs::path selected_output;
bool automatic_output = false;
std::string recent_import;
fs::path recent_source_directory;
std::vector<fs::path> recent_source_files;

JSValue path_string(JSContext* context, const fs::path& path) {
    const auto text = path.u8string();
    return JS_NewStringLen(context, reinterpret_cast<const char*>(text.data()), text.size());
}

JSValue js_image_dialog(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int mode) {
    if (!app_allows("desktop.filedrop") || !app_allows("fs.data"))
        return JS_ThrowTypeError(context, "image dialog permission denied");
    if (mode == 3) return path_string(context, app_data_root());
    if (mode == 4) {
        if (argc != 1 || !JS_IsString(argv[0])) return JS_ThrowTypeError(context, "input batch required");
        const auto input = js_to_string(context, argv[0]);
        if (input == "data:/input") {
            selected_output.clear();
            automatic_output = false;
            return path_string(context, app_data_root() / "output");
        }
        if (recent_source_directory.empty() || input != recent_import)
            return JS_ThrowTypeError(context, "no matching native image import; choose an output directory manually");
        selected_output = recent_source_directory / "output";
        automatic_output = true;
        return path_string(context, selected_output);
    }
#ifdef _WIN32
    if (mode == 1) {
        IFileDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            return JS_ThrowInternalError(context, "cannot open image picker");
        const COMDLG_FILTERSPEC filter[] = {{L"PNG / JPEG / WebP", L"*.png;*.jpg;*.jpeg;*.webp"}};
        dialog->SetFileTypes(1, filter);
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        const auto path = dialog_path(dialog);
        dialog->Release();
        return path.empty() ? JS_NULL : import_images(context, {wide(path)});
    }
    const auto path = pick_directory();
    if (path.empty()) return JS_NULL;
    if (mode == 0) return import_images(context, {wide(path)});
    selected_output = fs::u8path(path);
    automatic_output = false;
    return path_string(context, selected_output);
#else
    return JS_NULL;
#endif
}

JSValue js_publish_images(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (!app_allows("desktop.filedrop") || !app_allows("fs.data") || argc != 3 ||
        !JS_IsString(argv[0]) || !JS_IsString(argv[1]) || !JS_IsBool(argv[2]))
        return JS_ThrowTypeError(context, "invalid image export request");
    const auto input = js_to_string(context, argv[1]);
    if (input != "data:/input" && input != recent_import)
        return JS_ThrowTypeError(context, "input batch no longer matches the native import");
    const bool replace = JS_ToBool(context, argv[2]);
    const auto batch = js_to_string(context, argv[0]);
    const auto separator = batch.find('-');
    if (batch.size() > 40 || separator == 0 || separator == std::string::npos || separator + 1 == batch.size() ||
        batch.find_first_not_of("0123456789") != separator ||
        batch.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyz", separator + 1) != std::string::npos)
        return JS_ThrowTypeError(context, "invalid image batch");
    const auto root = app_data_root();
    const auto source = root / "output" / batch;
    const auto target = selected_output.empty() ? source.parent_path() : selected_output;
    std::error_code ec;
    if (!fs::is_directory(fs::symlink_status(source, ec))) return JS_ThrowInternalError(context, "image batch not found");
#ifdef _WIN32
    if (automatic_output) {
        fs::create_directory(target, ec);
        const DWORD attributes = GetFileAttributesW(target.c_str());
        if (ec || attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return JS_ThrowInternalError(context, "cannot use automatic output directory; choose a directory manually");
    }
#endif
    if (fs::equivalent(source, target, ec) || ec)
        return JS_ThrowInternalError(context, "invalid image output directory");

    // Copy on the destination volume before replacing any existing file.
    const auto staging = target / (".ykm-export-" + batch);
    if (!fs::create_directory(staging, ec)) return JS_ThrowInternalError(context, "cannot stage image export");
    const char* failure = nullptr;
    auto exists = [&](const fs::path& path) {
        const auto info = fs::symlink_status(path, ec);
        if (ec == std::errc::no_such_file_or_directory) ec.clear();
        return fs::exists(info);
    };
    for (auto it = fs::directory_iterator(source, ec), end = fs::directory_iterator(); !ec && it != end; it.increment(ec)) {
        const auto file = it->path();
        const auto ext = file.extension();
        if ((ext != ".jpg" && ext != ".png" && ext != ".webp") || !fs::is_regular_file(it->symlink_status(ec))) continue;
        auto destination = target / file.filename();
        if (replace && exists(destination)) {
            if (!fs::is_regular_file(fs::symlink_status(destination, ec)) || ec) {
                failure = "cannot replace a non-regular output file";
                break;
            }
            auto same_file = [&](const fs::path& original) {
                std::error_code error;
                return fs::equivalent(destination, original, error);
            };
            bool original = input == "data:/input" && same_file(root / "input" / file.filename());
            if (input != "data:/input") {
                for (const auto& file : recent_source_files) if (same_file(file)) { original = true; break; }
            }
            if (original) { failure = "cannot replace an original image; choose another directory or disable replacement"; break; }
        }
        if (ec) break;
        const auto temporary = staging / file.filename();
        if (!fs::copy_file(file, temporary, fs::copy_options::none, ec)) {
            failure = "could not stage an output image";
            break;
        }
        for (unsigned suffix = 2; ; ++suffix) {
#ifdef _WIN32
            if (MoveFileExW(temporary.c_str(), destination.c_str(), replace ? MOVEFILE_REPLACE_EXISTING : 0)) break;
            ec = std::error_code(GetLastError(), std::system_category());
#else
            if (replace) fs::rename(temporary, destination, ec);
            else fs::create_hard_link(temporary, destination, ec);
            if (!ec) break;
#endif
            if (replace || !exists(destination) || ec || suffix > 10000) {
                failure = "could not publish an output image";
                break;
            }
            auto name = file.stem();
            name += "-" + std::to_string(suffix);
            name += ext;
            destination = target / name;
        }
        if (failure) break;
    }
    std::error_code cleanup;
    fs::remove_all(staging, cleanup);
    if (failure) return JS_ThrowInternalError(context, "%s", failure);
    if (ec) return JS_ThrowInternalError(context, "could not export all images; the original output batch was retained");
    return path_string(context, target);
}

JSValue js_open_folder(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "openFolder requires a path");
    const std::string path = js_to_string(context, argv[0]);
#ifdef _WIN32
    const std::wstring wide_path = wide(path);
    ShellExecuteW(nullptr, L"explore", wide_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
    return JS_UNDEFINED;
}

JSValue js_run_tool(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic) {
    if (argc < 1) return JS_ThrowTypeError(context, "project directory required");
    auto* session = Session::from_context(context);
    if (!session) return JS_ThrowInternalError(context, "session is unavailable");
    const std::string project = js_to_string(context, argv[0]);
    std::string mode = "release";
    if (magic == 0 && argc >= 2) {
        if (JS_IsObject(argv[1])) {
            JSValue value = JS_GetPropertyStr(context, argv[1], "mode");
            if (!JS_IsUndefined(value)) mode = js_to_string(context, value);
            JS_FreeValue(context, value);
        } else {
            mode = js_to_string(context, argv[1]);
        }
    }
    if (mode != "debug" && mode != "release") mode = "release";
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(context, funcs);
    if (JS_IsException(promise)) return promise;
    JSValue resolve = funcs[0];
    JS_FreeValue(context, funcs[1]);
    const std::string verb = magic == 0 ? "package" : "create";
    std::thread([session, resolve, verb, project, mode]() {
        const CommandResult result = run_ykm(verb, project, mode);
        session->loop().enqueue_result(resolve, result.log, result.ok);
    }).detach();
    return promise;
}

} // namespace

void remember_image_source(const std::string& batch, const std::filesystem::path& directory,
                           std::vector<std::filesystem::path> files) {
    recent_import = "data:/" + batch;
    recent_source_directory = directory;
    recent_source_files = std::move(files);
}

void host_tools_install(JSContext* context, JSValue api) {
    JSValue dialog = JS_NewObject(context);
    JS_SetPropertyStr(context, dialog, "pickDirectory", JS_NewCFunction(context, js_pick_directory, "pickDirectory", 0));
    JS_SetPropertyStr(context, dialog, "pickImageDirectory", JS_NewCFunctionMagic(context, js_image_dialog, "pickImageDirectory", 0, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(context, dialog, "pickImage", JS_NewCFunctionMagic(context, js_image_dialog, "pickImage", 0, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(context, dialog, "pickImageOutput", JS_NewCFunctionMagic(context, js_image_dialog, "pickImageOutput", 0, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(context, dialog, "imageDataDirectory", JS_NewCFunctionMagic(context, js_image_dialog, "imageDataDirectory", 0, JS_CFUNC_generic_magic, 3));
    JS_SetPropertyStr(context, dialog, "autoImageOutput", JS_NewCFunctionMagic(context, js_image_dialog, "autoImageOutput", 1, JS_CFUNC_generic_magic, 4));
    JS_SetPropertyStr(context, dialog, "publishImages", JS_NewCFunction(context, js_publish_images, "publishImages", 3));
    JS_SetPropertyStr(context, dialog, "openFile", JS_NewCFunction(context, js_open_file, "openFile", 0));
    JS_SetPropertyStr(context, dialog, "saveFile", JS_NewCFunction(context, js_save_file, "saveFile", 2));
    JS_SetPropertyStr(context, api, "dialog", dialog);
    JSValue tools = JS_NewObject(context);
    JS_SetPropertyStr(context, tools, "package", JS_NewCFunctionMagic(context, js_run_tool, "package", 2, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(context, tools, "create", JS_NewCFunctionMagic(context, js_run_tool, "create", 1, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(context, tools, "openFolder", JS_NewCFunction(context, js_open_folder, "openFolder", 1));
    JS_SetPropertyStr(context, api, "tools", tools);
}

} // namespace ykm
