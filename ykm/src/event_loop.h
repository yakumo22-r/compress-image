#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <quickjs.h>
}

namespace ykm {

class EventLoop {
public:
    using Clock = std::chrono::steady_clock;

    EventLoop() = default;
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void attach(JSContext* context);
    void shutdown();

    uint32_t set_timer(JSValueConst callback, int64_t delay_ms, bool repeat);
    void clear_timer(uint32_t id);
    void enqueue_call(JSValue func, JSValue argument);
    void enqueue_result(JSValue func, std::string text, bool ok);

    bool pump();
    bool has_work() const;
    std::optional<Clock::duration> time_until_next() const;

    static JSValue js_set_timeout(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_set_interval(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_clear_timer(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);
    static JSValue js_queue_microtask(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv);

private:
    struct Timer {
        uint32_t id = 0;
        JSValue callback = JS_UNDEFINED;
        Clock::time_point when{};
        Clock::duration interval{};
        bool repeat = false;
        bool cancelled = false;
    };

    struct Completion {
        JSValue func = JS_UNDEFINED;
        JSValue argument = JS_UNDEFINED;
    };

    struct Result {
        JSValue func = JS_UNDEFINED;
        std::string text;
        bool ok = false;
    };

    static JSValue job_call(JSContext* context, int argc, JSValueConst* argv);

    JSContext* context_ = nullptr;
    std::vector<Timer> timers_;
    std::vector<Completion> completions_;
    std::vector<Result> results_;
    mutable std::mutex mutex_;
    uint32_t next_id_ = 1;
};

void event_loop_install_timers(JSContext* context);

} // namespace ykm
