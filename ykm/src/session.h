#pragma once

#include "event_loop.h"
#include "module_loader.h"
#include "api_unit.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

extern "C" {
#include <quickjs.h>
}

namespace ykm {

class SessionManager;

struct SessionDefinition {
    std::string id;
    std::string role;
    std::string path;
    bool embedded = false;
};

struct Message {
    std::string sender;
    std::string json;
};

class Session {
public:
    Session(SessionManager& manager, std::string id, const std::atomic_bool* stop = nullptr);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    static Session* from_context(JSContext* context);

    bool load(const std::string& path, bool require_render = true);
    bool load_embedded(const std::string& module, bool require_render = true);
    JSValue render();
    int run_cli(const std::vector<std::string>& arguments);
    void eval_console(const std::string& source);
    void set_arguments(std::vector<std::string> arguments) { arguments_ = std::move(arguments); }
    bool enqueue(Message message);
    bool is_worker() const { return stop_ != nullptr; }
    bool stop_requested() const { return stop_ && stop_->load(); }
    void dispatch_messages();
    void drain_jobs();
    void pump();
    void run_until_idle();

    JSContext* context() const { return context_; }
    bool has_render() const { return JS_IsFunction(context_, render_function_); }
    const std::string& id() const { return id_; }
    const std::string& error() const { return error_; }
    void report_exception(std::string_view operation);

    EventLoop& loop() { return loop_; }
    std::unordered_map<std::string, JSValue>& modules() { return modules_; }
    const std::filesystem::path& root() const { return root_; }
    void set_root(std::filesystem::path root) { root_ = std::move(root); }

private:
    static JSValue js_send(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_print(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_error(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_logging_enable(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_logging_disable(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_logging_status(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_start_session(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_is_session_running(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_stop_worker(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_list_sessions(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    bool install_api();
    bool evaluate_entry(const std::string& source, const std::string& name, bool is_module, bool require_render);
    void print_value(JSValueConst value);

    SessionManager& manager_;
    std::string id_;
    std::filesystem::path root_;
    const std::atomic_bool* stop_;
    JSRuntime* runtime_ = nullptr;
    JSContext* context_ = nullptr;
    JSValue render_function_ = JS_UNDEFINED;
    JSValue message_function_ = JS_UNDEFINED;
    std::mutex inbox_mutex_;
    std::deque<Message> inbox_;
    size_t inbox_bytes_ = 0;
    std::vector<std::string> arguments_;
    std::string error_;
    EventLoop loop_;
    ModuleLoader module_loader_;
    std::unordered_map<std::string, JSValue> modules_;
};

class SessionManager {
public:
    ~SessionManager();
    void shutdown_workers();
    bool stop_worker(const std::string& id);
    void add_api_unit(const ApiUnit& unit);
    const std::vector<ApiUnit>& api_units() const { return api_units_; }
    void set_root(std::filesystem::path root) { root_ = std::move(root); }
    const std::filesystem::path& root() const { return root_; }
    bool add(std::string id, const std::string& path, bool require_render = true);
    bool register_session(SessionDefinition definition);
    bool start(const std::string& id);
    void start_pending();
    bool is_running(const std::string& id) const;
    std::vector<std::string> registered_sessions() const;
    enum class SendResult { Queued, Unavailable, Full };
    SendResult send(const std::string& sender, const std::string& target, std::string json);
    void dispatch_messages();
    void drain_jobs();
    void pump();
    void execute_console(const std::string& command);
    const std::vector<std::unique_ptr<Session>>& sessions() const { return sessions_; }

private:
    Session* find_running(const std::string& id) const;
    struct Worker {
        std::thread thread;
        std::atomic_bool stop{false};
        bool ready = false;
        bool finished = false;
        std::deque<Message> inbox;
        size_t inbox_bytes = 0;
    };
    void run_worker(Worker& worker, const SessionDefinition& definition);
    mutable std::mutex mutex_;
    std::condition_variable worker_wakeup_;
    std::unordered_map<std::string, std::unique_ptr<Worker>> workers_;
    bool shutting_down_ = false;

    std::filesystem::path root_ = std::filesystem::current_path();
    std::vector<SessionDefinition> definitions_;
    std::deque<std::string> pending_starts_;
    std::vector<std::unique_ptr<Session>> sessions_;
    std::vector<ApiUnit> api_units_;
};

} // namespace ykm
