#include "session.h"

#include "app_storage.h"
#include "debug_log.h"
#include "event_loop.h"
#include "js_util.h"
#include "jsapi.h"

#include <ykm/embedded_app.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

namespace ykm {

constexpr size_t kInboxMessages = 256;
constexpr size_t kInboxBytes = 4 * 1024 * 1024;
constexpr size_t kMessagesPerTurn = 32;

static bool queue_message(std::deque<Message>& inbox, size_t& bytes, Message message) {
    const size_t size = message.json.size() + message.sender.size();
    if (inbox.size() >= kInboxMessages || size > kInboxBytes - bytes) return false;
    inbox.push_back(std::move(message));
    bytes += size;
    return true;
}
namespace {

bool path_within(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::string root_text = root.generic_string();
    std::string path_text = path.generic_string();
#ifdef _WIN32
    std::transform(root_text.begin(), root_text.end(), root_text.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(path_text.begin(), path_text.end(), path_text.begin(), [](unsigned char c) { return std::tolower(c); });
#endif
    if (path_text == root_text) return true;
    if (!root_text.empty() && root_text.back() != '/') root_text.push_back('/');
    return path_text.size() >= root_text.size() && path_text.compare(0, root_text.size(), root_text) == 0;
}

bool resolve_log_path(Session& session, const std::string& requested, std::filesystem::path& result,
                      std::string& error) {
    namespace fs = std::filesystem;
    if (requested.empty() || requested.rfind("asset:/", 0) == 0) {
        error = "logging.enable(path) requires a writable path";
        return false;
    }
    fs::path root;
    std::string relative = requested;
    const char* permission = "fs.app";
    if (requested.rfind("data:/", 0) == 0) {
        root = app_data_root();
        relative = requested.substr(6);
        permission = "fs.data";
    } else if (requested.rfind("cache:/", 0) == 0) {
        root = app_cache_root();
        relative = requested.substr(7);
        permission = "fs.cache";
    } else {
        root = session.root();
    }
    if (!app_allows(permission)) {
        error = std::string("permission denied: ") + permission;
        return false;
    }
    root = fs::absolute(root).lexically_normal();
    const fs::path input(relative);
    result = (input.is_absolute() ? input : root / input).lexically_normal();
    if (!path_within(root, result)) {
        error = "log path escapes application root";
        return false;
    }
    return true;
}

} // namespace

Session* Session::from_context(JSContext* context) {
    return context ? static_cast<Session*>(JS_GetContextOpaque(context)) : nullptr;
}

Session::Session(SessionManager& manager, std::string id, const std::atomic_bool* stop)
    : manager_(manager), id_(std::move(id)), root_(manager.root()), stop_(stop),
      runtime_(JS_NewRuntime()), context_(runtime_ ? JS_NewContext(runtime_) : nullptr) {
    if (context_) {
        JS_SetContextOpaque(context_, this);
        JS_SetMemoryLimit(runtime_, 32 * 1024 * 1024);
        JS_SetMaxStackSize(runtime_, 512 * 1024);
        if (is_worker()) JS_SetInterruptHandler(runtime_, [](JSRuntime*, void* opaque) {
            return static_cast<Session*>(opaque)->stop_requested() ? 1 : 0;
        }, this);
        module_loader_.install(runtime_, root_);
    }
}

Session::~Session() {
    if (context_) {
        loop_.shutdown();
        jsapi_free_modules(modules_, context_);
        JS_FreeValue(context_, render_function_);
        JS_FreeValue(context_, message_function_);
        JS_FreeContext(context_);
    }
    if (runtime_) {
        JS_FreeRuntime(runtime_);
    }
}

bool Session::install_api() {
    loop_.attach(context_);
    event_loop_install_timers(context_);
    jsapi_install_require(context_);
    JSValue global = JS_GetGlobalObject(context_);
    JSValue api = JS_NewObject(context_);
    JS_SetPropertyStr(context_, api, "send", JS_NewCFunction(context_, js_send, "send", 2));
    JS_SetPropertyStr(context_, api, "print", JS_NewCFunction(context_, js_print, "print", 1));
    JS_SetPropertyStr(context_, api, "error", JS_NewCFunction(context_, js_error, "error", 1));
    JSValue logging = JS_NewObject(context_);
    JS_SetPropertyStr(context_, logging, "enable", JS_NewCFunction(context_, js_logging_enable, "enable", 1));
    JS_SetPropertyStr(context_, logging, "disable", JS_NewCFunction(context_, js_logging_disable, "disable", 0));
    JS_SetPropertyStr(context_, logging, "status", JS_NewCFunction(context_, js_logging_status, "status", 0));
    JS_SetPropertyStr(context_, api, "logging", logging);
    JS_SetPropertyStr(context_, api, "sessionId", JS_NewString(context_, id_.c_str()));
    JSValue args = JS_NewArray(context_);
    for (uint32_t index = 0; index < arguments_.size(); ++index) {
        JS_SetPropertyUint32(context_, args, index, JS_NewString(context_, arguments_[index].c_str()));
    }
    JS_SetPropertyStr(context_, api, "args", args);
    JSValue sessions = JS_NewObject(context_);
    JS_SetPropertyStr(context_, sessions, "start", JS_NewCFunction(context_, js_start_session, "start", 1));
    JS_SetPropertyStr(context_, sessions, "stop", JS_NewCFunction(context_, js_stop_worker, "stop", 1));
    JS_SetPropertyStr(context_, sessions, "isRunning", JS_NewCFunction(context_, js_is_session_running, "isRunning", 1));
    JS_SetPropertyStr(context_, sessions, "list", JS_NewCFunction(context_, js_list_sessions, "list", 0));
    JS_SetPropertyStr(context_, api, "sessions", sessions);
    JSValue console = JS_NewObject(context_);
    JS_SetPropertyStr(context_, console, "log", JS_NewCFunction(context_, js_print, "log", 1));
    JS_SetPropertyStr(context_, console, "error", JS_NewCFunction(context_, js_error, "error", 1));
    JS_SetPropertyStr(context_, global, "console", console);
    int result = JS_SetPropertyStr(context_, global, "ykm", api);
    JS_FreeValue(context_, global);
    if (result < 0) return false;
    if (is_worker()) return true;
    for (const ApiUnit& unit : manager_.api_units()) {
        if (!unit.install || !unit.install(*this)) {
            error_ = std::string("unable to install API unit: ") + (unit.name ? unit.name : "<unnamed>");
            return false;
        }
    }
    return true;
}

bool Session::load(const std::string& path, bool require_render) {
    if (!context_) {
        error_ = "unable to create QuickJS runtime";
        return false;
    }

    const std::filesystem::path absolute_path = std::filesystem::absolute(path).lexically_normal();
    std::ifstream input(absolute_path, std::ios::binary);
    if (!input) {
        error_ = "unable to open " + absolute_path.string();
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    const std::string source = stream.str();

    return evaluate_entry(source, absolute_path.generic_string(), source.rfind("/* ykm:module */", 0) == 0,
                          require_render);
}

bool Session::load_embedded(const std::string& module, bool require_render) {
    if (!context_) {
        error_ = "unable to create QuickJS runtime";
        return false;
    }
    const YkmEmbeddedApp* app = ykm_get_embedded_app();
    if (!app) {
        error_ = "embedded application is unavailable";
        return false;
    }
    for (uint32_t index = 0; index < app->module_count; ++index) {
        if (module == app->modules[index].name) {
            const std::string source(reinterpret_cast<const char*>(app->modules[index].data), app->modules[index].size);
            return evaluate_entry(source, module, true, require_render);
        }
    }
    error_ = "embedded module not found: " + module;
    return false;
}

bool Session::evaluate_entry(const std::string& source, const std::string& name, bool is_module,
                             bool require_render) {
    if (!install_api()) {
        report_exception("install API");
        return false;
    }

    JSValue result = JS_Eval(context_, source.data(), source.size(), name.c_str(),
                             is_module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JS_FreeValue(context_, result);
        report_exception("evaluate script");
        return false;
    }
    if (is_worker() && is_module) {
        while (!stop_requested() && JS_PromiseState(context_, result) == JS_PROMISE_PENDING) {
            pump();
            if (JS_PromiseState(context_, result) == JS_PROMISE_PENDING)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (stop_requested() || JS_PromiseState(context_, result) == JS_PROMISE_REJECTED) {
            if (!stop_requested()) {
                JS_Throw(context_, JS_PromiseResult(context_, result));
                report_exception("evaluate worker module");
            }
            JS_FreeValue(context_, result);
            return false;
        }
    }
    JS_FreeValue(context_, result);

    JSValue global = JS_GetGlobalObject(context_);
    render_function_ = JS_GetPropertyStr(context_, global, "render");
    message_function_ = JS_GetPropertyStr(context_, global, "onMessage");
    JS_FreeValue(context_, global);
    if (require_render && !JS_IsFunction(context_, render_function_)) {
        error_ = name + " must define global function render()";
        return false;
    }
    return true;
}

int Session::run_cli(const std::vector<std::string>& arguments) {
    arguments_ = arguments;
    JSValue global = JS_GetGlobalObject(context_);
    JSValue api = JS_GetPropertyStr(context_, global, "ykm");
    JSValue ykm_args = JS_NewArray(context_);
    for (uint32_t index = 0; index < arguments.size(); ++index) {
        JS_SetPropertyUint32(context_, ykm_args, index, JS_NewString(context_, arguments[index].c_str()));
    }
    JS_SetPropertyStr(context_, api, "args", ykm_args);
    JS_FreeValue(context_, api);
    JSValue main_function = JS_GetPropertyStr(context_, global, "main");
    JS_FreeValue(context_, global);
    if (!JS_IsFunction(context_, main_function)) {
        JS_FreeValue(context_, main_function);
        return 0;
    }

    JSValue args = JS_NewArray(context_);
    for (uint32_t index = 0; index < arguments.size(); ++index) {
        JS_SetPropertyUint32(context_, args, index, JS_NewString(context_, arguments[index].c_str()));
    }
    JSValue result = JS_Call(context_, main_function, JS_UNDEFINED, 1, &args);
    JS_FreeValue(context_, args);
    JS_FreeValue(context_, main_function);
    if (JS_IsException(result)) {
        report_exception("main");
        JS_FreeValue(context_, result);
        return 1;
    }

    if (JS_IsObject(result)) {
        JSValue global = JS_GetGlobalObject(context_);
        JSValue promise_constructor = JS_GetPropertyStr(context_, global, "Promise");
        const bool is_promise = JS_IsInstanceOf(context_, result, promise_constructor) == 1;
        JS_FreeValue(context_, promise_constructor);
        JS_FreeValue(context_, global);
        if (is_promise) {
            while (JS_PromiseState(context_, result) == JS_PROMISE_PENDING) {
                drain_jobs();
                loop_.pump();
                drain_jobs();
                if (JS_PromiseState(context_, result) != JS_PROMISE_PENDING) break;
                if (runtime_ && JS_IsJobPending(runtime_)) continue;
                const auto wait = loop_.time_until_next();
                if (!wait && !loop_.has_work()) {
                    const std::string message = id_ + ": main returned a pending Promise with no runnable jobs";
                    std::cerr << message << '\n';
                    debug_log(LogLevel::Error, message);
                    JS_FreeValue(context_, result);
                    return 1;
                }
                if (wait && *wait > EventLoop::Clock::duration::zero()) {
                    std::this_thread::sleep_for(*wait);
                }
            }
            const bool rejected = JS_PromiseState(context_, result) == JS_PROMISE_REJECTED;
            JSValue settled = JS_PromiseResult(context_, result);
            JS_FreeValue(context_, result);
            result = settled;
            if (rejected) {
                error_ = id_ + ": async main rejected: " + js_to_string(context_, result);
                std::cerr << error_ << '\n';
                debug_log(LogLevel::Error, error_);
                JS_FreeValue(context_, result);
                return 1;
            }
        }
    }

    int32_t exit_code = 0;
    if (JS_IsNumber(result)) {
        JS_ToInt32(context_, &exit_code, result);
    } else if (!JS_IsUndefined(result) && !JS_IsNull(result)) {
        print_value(result);
    }
    JS_FreeValue(context_, result);
    run_until_idle();
    return exit_code;
}

void Session::eval_console(const std::string& source) {
    JSValue result = JS_Eval(context_, source.data(), source.size(), "<debug-console>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JS_FreeValue(context_, result);
        report_exception("console eval");
        return;
    }

    if (JS_IsObject(result)) {
        JSValue global = JS_GetGlobalObject(context_);
        JSValue promise_constructor = JS_GetPropertyStr(context_, global, "Promise");
        const bool is_promise = JS_IsInstanceOf(context_, result, promise_constructor) == 1;
        JS_FreeValue(context_, promise_constructor);
        JS_FreeValue(context_, global);
        if (is_promise) {
            while (JS_PromiseState(context_, result) == JS_PROMISE_PENDING) {
                pump();
                if (JS_PromiseState(context_, result) != JS_PROMISE_PENDING) break;
                const auto wait = loop_.time_until_next();
                if (!wait && !loop_.has_work()) break;
                if (wait && *wait > EventLoop::Clock::duration::zero()) std::this_thread::sleep_for(*wait);
            }
            if (JS_PromiseState(context_, result) == JS_PROMISE_REJECTED) {
                JSValue rejected = JS_PromiseResult(context_, result);
                debug_log(LogLevel::Error, js_to_output(context_, rejected));
                JS_FreeValue(context_, rejected);
                JS_FreeValue(context_, result);
                return;
            }
            if (JS_PromiseState(context_, result) == JS_PROMISE_FULFILLED) {
                JSValue fulfilled = JS_PromiseResult(context_, result);
                JS_FreeValue(context_, result);
                result = fulfilled;
            }
        }
    }
    if (!JS_IsUndefined(result)) debug_log(LogLevel::Result, js_to_output(context_, result));
    JS_FreeValue(context_, result);
}

void Session::pump() {
    loop_.pump();
    drain_jobs();
}

void Session::run_until_idle() {
    for (;;) {
        drain_jobs();
        loop_.pump();
        drain_jobs();
        if (runtime_ && JS_IsJobPending(runtime_)) continue;
        if (!loop_.has_work()) break;
        const auto wait = loop_.time_until_next();
        if (!wait) break;
        if (*wait > EventLoop::Clock::duration::zero()) {
            std::this_thread::sleep_for(*wait);
        }
    }
}

JSValue Session::render() {
    JSValue value = JS_Call(context_, render_function_, JS_UNDEFINED, 0, nullptr);
    if (JS_IsException(value)) {
        report_exception("render");
    }
    return value;
}

bool Session::enqueue(Message message) {
    std::lock_guard<std::mutex> lock(inbox_mutex_);
    return queue_message(inbox_, inbox_bytes_, std::move(message));
}

void Session::dispatch_messages() {
    const bool handles_messages = JS_IsFunction(context_, message_function_);
    for (size_t n = 0; n < kMessagesPerTurn && !stop_requested(); ++n) {
        Message message;
        {
            std::lock_guard<std::mutex> lock(inbox_mutex_);
            if (inbox_.empty()) break;
            message = std::move(inbox_.front());
            inbox_.pop_front();
            inbox_bytes_ -= message.sender.size() + message.json.size();
        }
        if (!handles_messages) continue;
        JSValue payload = JS_ParseJSON(context_, message.json.data(), message.json.size(), "<message>");
        if (JS_IsException(payload)) {
            report_exception("parse message");
            continue;
        }
        JSValue sender = JS_NewString(context_, message.sender.c_str());
        JSValue arguments[] = {payload, sender};
        JSValue result = JS_Call(context_, message_function_, JS_UNDEFINED, 2, arguments);
        JS_FreeValue(context_, sender);
        JS_FreeValue(context_, payload);
        if (JS_IsException(result)) {
            report_exception("onMessage");
        }
        JS_FreeValue(context_, result);
    }
}

void Session::drain_jobs() {
    JSContext* job_context = nullptr;
    while (!stop_requested()) {
        const int status = JS_ExecutePendingJob(runtime_, &job_context);
        if (status == 0) break;
        if (status < 0 && job_context) report_exception("job");
    }
}

void Session::report_exception(std::string_view operation) {
    JSValue exception = JS_GetException(context_);
    if (stop_requested()) {
        JS_FreeValue(context_, exception);
        return;
    }
    error_ = id_ + ": " + std::string(operation) + ": " + js_to_string(context_, exception);
    if (JS_IsError(context_, exception)) {
        JSValue stack = JS_GetPropertyStr(context_, exception, "stack");
        const std::string stack_text = js_to_string(context_, stack);
        if (!stack_text.empty()) {
            error_ += "\n" + stack_text;
        }
        JS_FreeValue(context_, stack);
    }
    JS_FreeValue(context_, exception);
    std::cerr << error_ << '\n';
    debug_log(LogLevel::Error, error_);
}

JSValue Session::js_send(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    auto* session = static_cast<Session*>(JS_GetContextOpaque(context));
    if (!session || argc < 2) {
        return JS_ThrowTypeError(context, "send(target, payload) requires two arguments");
    }
    const char* target_text = JS_ToCString(context, argv[0]);
    if (!target_text) return JS_EXCEPTION;
    const std::string target(target_text);
    JS_FreeCString(context, target_text);
    JSValue global = JS_GetGlobalObject(context);
    JSValue json_object = JS_GetPropertyStr(context, global, "JSON");
    JSValue stringify = JS_GetPropertyStr(context, json_object, "stringify");
    JSValue argument = JS_DupValue(context, argv[1]);
    JSValue json_value = JS_Call(context, stringify, json_object, 1, &argument);
    JS_FreeValue(context, argument);
    JS_FreeValue(context, stringify);
    JS_FreeValue(context, json_object);
    JS_FreeValue(context, global);
    if (JS_IsException(json_value)) {
        return json_value;
    }
    if (JS_IsUndefined(json_value)) {
        JS_FreeValue(context, json_value);
        return JS_ThrowTypeError(context, "payload is not structured-cloneable");
    }
    std::string json = js_to_string(context, json_value);
    JS_FreeValue(context, json_value);
    switch (session->manager_.send(session->id_, target, std::move(json))) {
    case SessionManager::SendResult::Unavailable:
        return JS_ThrowReferenceError(context, "target session '%s' is unavailable", target.c_str());
    case SessionManager::SendResult::Full:
        return JS_ThrowRangeError(context, "target session '%s' inbox limit exceeded", target.c_str());
    case SessionManager::SendResult::Queued:
        break;
    }
    return JS_UNDEFINED;
}

JSValue Session::js_print(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    std::string line;
    for (int index = 0; index < argc; ++index) {
        if (index > 0) std::cout << ' ';
        const std::string value = js_to_output(context, argv[index]);
        if (index > 0) line += ' ';
        line += value;
        std::cout << value;
    }
    std::cout << '\n';
    debug_log(LogLevel::Info, std::move(line));
    return JS_UNDEFINED;
}

JSValue Session::js_error(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    std::string line;
    for (int index = 0; index < argc; ++index) {
        if (index > 0) std::cerr << ' ';
        const std::string value = js_to_output(context, argv[index]);
        if (index > 0) line += ' ';
        line += value;
        std::cerr << value;
    }
    std::cerr << '\n';
    debug_log(LogLevel::Error, std::move(line));
    return JS_UNDEFINED;
}

JSValue Session::js_logging_enable(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    Session* session = from_context(context);
    if (!session || argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(context, "logging.enable(path) requires a string path");
    }
    const std::string requested = js_to_string(context, argv[0]);
    std::filesystem::path path;
    std::string error;
    if (!resolve_log_path(*session, requested, path, error) || !debug_log_open_file(path, error)) {
        return JS_ThrowInternalError(context, "%s", error.c_str());
    }
    return JS_NewString(context, debug_log_file_path().c_str());
}

JSValue Session::js_logging_disable(JSContext*, JSValueConst, int, JSValueConst*) {
    debug_log_close_file();
    return JS_UNDEFINED;
}

JSValue Session::js_logging_status(JSContext* context, JSValueConst, int, JSValueConst*) {
    JSValue result = JS_NewObject(context);
    const bool enabled = debug_log_file_enabled();
    JS_SetPropertyStr(context, result, "enabled", JS_NewBool(context, enabled));
    if (enabled) JS_SetPropertyStr(context, result, "path", JS_NewString(context, debug_log_file_path().c_str()));
    else JS_SetPropertyStr(context, result, "path", JS_NULL);
    return result;
}

JSValue Session::js_start_session(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    auto* session = static_cast<Session*>(JS_GetContextOpaque(context));
    if (!session || argc < 1) return JS_ThrowTypeError(context, "sessions.start(name) requires a name");
    const char* name = JS_ToCString(context, argv[0]);
    if (!name) return JS_EXCEPTION;
    const bool started = session->manager_.start(name);
    JS_FreeCString(context, name);
    return JS_NewBool(context, started);
}

JSValue Session::js_is_session_running(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    auto* session = static_cast<Session*>(JS_GetContextOpaque(context));
    if (!session || argc < 1) return JS_ThrowTypeError(context, "sessions.isRunning(name) requires a name");
    const char* name = JS_ToCString(context, argv[0]);
    if (!name) return JS_EXCEPTION;
    const bool running = session->manager_.is_running(name);
    JS_FreeCString(context, name);
    return JS_NewBool(context, running);
}

JSValue Session::js_stop_worker(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    auto* session = from_context(context);
    if (!session || argc < 1) return JS_ThrowTypeError(context, "sessions.stop(name) requires a name");
    const char* name = JS_ToCString(context, argv[0]);
    if (!name) return JS_EXCEPTION;
    const bool stopped = session->manager_.stop_worker(name);
    JS_FreeCString(context, name);
    return JS_NewBool(context, stopped);
}

JSValue Session::js_list_sessions(JSContext* context, JSValueConst, int, JSValueConst*) {
    auto* session = static_cast<Session*>(JS_GetContextOpaque(context));
    if (!session) return JS_ThrowInternalError(context, "session is unavailable");
    JSValue result = JS_NewArray(context);
    const auto names = session->manager_.registered_sessions();
    for (uint32_t index = 0; index < names.size(); ++index) {
        JS_SetPropertyUint32(context, result, index, JS_NewString(context, names[index].c_str()));
    }
    return result;
}

void Session::print_value(JSValueConst value) {
    const std::string output = js_to_output(context_, value);
    std::cout << output << '\n';
    debug_log(LogLevel::Result, output);
}

SessionManager::~SessionManager() { shutdown_workers(); }

void SessionManager::shutdown_workers() {
    decltype(workers_) workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutting_down_ = true;
        pending_starts_.clear();
        for (auto& entry : workers_) entry.second->stop.store(true);
        workers.swap(workers_);
    }
    worker_wakeup_.notify_all();
    for (auto& entry : workers) entry.second->thread.join();
}

void SessionManager::run_worker(Worker& worker, const SessionDefinition& definition) {
    try {
        Session session(*this, definition.id, &worker.stop);
        const bool loaded = !session.stop_requested() && (definition.embedded
            ? session.load_embedded(definition.path, false) : session.load(definition.path, false));
        if (loaded) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                worker.ready = true;
            }
            while (!session.stop_requested()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (size_t n = 0; n < kMessagesPerTurn && !worker.inbox.empty(); ++n) {
                        auto& message = worker.inbox.front();
                        worker.inbox_bytes -= message.sender.size() + message.json.size();
                        session.enqueue(std::move(message));
                        worker.inbox.pop_front();
                    }
                }
                session.dispatch_messages();
                if (session.stop_requested()) break;
                session.pump();
                std::unique_lock<std::mutex> lock(mutex_);
                const auto ready = [&] { return session.stop_requested() || !worker.inbox.empty(); };
                if (const auto delay = session.loop().time_until_next()) {
                    worker_wakeup_.wait_for(lock, *delay, ready);
                } else {
                    worker_wakeup_.wait(lock, ready);
                }
            }
        } else if (!session.stop_requested()) {
            const std::string error = definition.id + ": worker startup failed: " + session.error();
            std::cerr << error << '\n';
            debug_log(LogLevel::Error, error);
        }
    } catch (const std::exception& error) {
        const std::string text = definition.id + ": worker failed: " + error.what();
        std::cerr << text << '\n';
        debug_log(LogLevel::Error, text);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    worker.inbox.clear();
    worker.inbox_bytes = 0;
    worker.finished = true;
}

bool SessionManager::stop_worker(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = workers_.find(id);
    if (found != workers_.end() && !found->second->finished) {
        found->second->stop.store(true);
        worker_wakeup_.notify_all();
        return true;
    }
    const auto pending = std::find(pending_starts_.begin(), pending_starts_.end(), id);
    if (pending == pending_starts_.end() || std::none_of(definitions_.begin(), definitions_.end(),
        [&](const SessionDefinition& def) { return def.id == id && def.role == "worker"; })) return false;
    pending_starts_.erase(pending);
    return true;
}

bool SessionManager::add(std::string id, const std::string& path, bool require_render) {
    auto session = std::make_unique<Session>(*this, std::move(id));
    if (!session->load(path, require_render)) {
        std::cerr << session->error() << '\n';
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.push_back(std::move(session));
    return true;
}

void SessionManager::add_api_unit(const ApiUnit& unit) {
    if (!unit.install) return;
    const auto existing = std::find_if(api_units_.begin(), api_units_.end(), [&](const ApiUnit& candidate) {
        return candidate.name && unit.name && std::string(candidate.name) == unit.name;
    });
    if (existing == api_units_.end()) api_units_.push_back(unit);
}

bool SessionManager::register_session(SessionDefinition definition) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& existing : definitions_) {
        if (existing.id == definition.id) return false;
    }
    definitions_.push_back(std::move(definition));
    return true;
}

bool SessionManager::start(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutting_down_) return false;
    if (find_running(id) || std::find(pending_starts_.begin(), pending_starts_.end(), id) != pending_starts_.end())
        return true;
    const auto worker = workers_.find(id);
    if (worker != workers_.end() && !worker->second->finished) return !worker->second->stop.load();
    for (const auto& definition : definitions_) {
        if (definition.id == id) {
            pending_starts_.push_back(id);
            return true;
        }
    }
    return false;
}

void SessionManager::start_pending() {
    for (;;) {
        SessionDefinition definition;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = workers_.begin(); it != workers_.end();) {
                if (!it->second->finished) { ++it; continue; }
                // A finished worker has released the runtime and will not take mutex_ again.
                it->second->thread.join();
                it = workers_.erase(it);
            }
            if (pending_starts_.empty() || shutting_down_) return;
            const std::string id = std::move(pending_starts_.front());
            pending_starts_.pop_front();
            if (find_running(id)) continue;
            const auto active = workers_.find(id);
            if (active != workers_.end()) continue;
            const auto found = std::find_if(definitions_.begin(), definitions_.end(),
                [&](const SessionDefinition& def) { return def.id == id; });
            if (found == definitions_.end()) continue;
            definition = *found;
            if (definition.role == "worker") {
                auto& slot = workers_[id];
                slot = std::make_unique<Worker>();
                try {
                    slot->thread = std::thread([this, worker = slot.get(), definition] {
                        run_worker(*worker, definition);
                    });
                } catch (const std::system_error& error) {
                    workers_.erase(id);
                    debug_log(LogLevel::Error, id + ": cannot start worker thread: " + error.what());
                }
                continue;
            }
        }
        if (!definition.embedded) {
            add(definition.id, definition.path, definition.role == "ui");
            continue;
        }
        auto session = std::make_unique<Session>(*this, definition.id);
        if (!session->load_embedded(definition.path, definition.role == "ui")) {
            std::cerr << session->error() << '\n';
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_.push_back(std::move(session));
        }
    }
}

bool SessionManager::is_running(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto worker = workers_.find(id);
    if (worker != workers_.end())
        return worker->second->ready && !worker->second->finished && !worker->second->stop.load();
    return find_running(id) != nullptr;
}

std::vector<std::string> SessionManager::registered_sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    result.reserve(definitions_.size());
    for (const auto& definition : definitions_) result.push_back(definition.id);
    return result;
}

SessionManager::SendResult SessionManager::send(const std::string& sender, const std::string& target, std::string json) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutting_down_) return SendResult::Unavailable;
    const auto it = workers_.find(target);
    if (it != workers_.end()) {
        auto& worker = *it->second;
        if (worker.stop.load() || worker.finished) return SendResult::Unavailable;
        if (!queue_message(worker.inbox, worker.inbox_bytes, {sender, std::move(json)}))
            return SendResult::Full;
        worker_wakeup_.notify_all();
        return SendResult::Queued;
    }
    if (auto* session = find_running(target))
        return session->enqueue({sender, std::move(json)}) ? SendResult::Queued : SendResult::Full;
    return SendResult::Unavailable;
}

void SessionManager::dispatch_messages() {
    for (const auto& session : sessions_) {
        session->dispatch_messages();
    }
}

void SessionManager::drain_jobs() {
    for (const auto& session : sessions_) {
        session->drain_jobs();
    }
}

void SessionManager::pump() {
    for (const auto& session : sessions_) {
        session->pump();
    }
}

Session* SessionManager::find_running(const std::string& id) const {
    for (const auto& session : sessions_) {
        if (session->id() == id) return session.get();
    }
    return nullptr;
}

void SessionManager::execute_console(const std::string& command) {
    const SessionDefinition* cli = nullptr;
    const SessionDefinition* main = nullptr;
    for (const auto& definition : definitions_) {
        if (definition.role == "cli" && !cli) cli = &definition;
        if (definition.role == "main" && !main) main = &definition;
    }

    if (cli) {
        Session* session = find_running(cli->id);
        if (!session) {
            start(cli->id);
            start_pending();
            session = find_running(cli->id);
        }
        if (!session) {
            debug_log(LogLevel::Error, "unable to start CLI session");
            return;
        }

        std::vector<std::string> arguments;
        std::string current;
        char quote = 0;
        for (char c : command) {
            if (quote) {
                if (c == quote) quote = 0;
                else current.push_back(c);
            } else if (c == '\'' || c == '"') {
                quote = c;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                if (!current.empty()) {
                    arguments.push_back(std::move(current));
                    current.clear();
                }
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) arguments.push_back(std::move(current));
        const int code = session->run_cli(arguments);
        debug_log(LogLevel::Result, "CLI exit code: " + std::to_string(code));
        return;
    }

    Session* target = main ? find_running(main->id) : nullptr;
    if (!target && !sessions_.empty()) target = sessions_.front().get();
    if (!target) {
        debug_log(LogLevel::Error, "no session available for JavaScript evaluation");
        return;
    }
    target->eval_console(command);
}

} // namespace ykm
