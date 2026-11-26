#include "event_loop.h"

#include "session.h"

#include <algorithm>
#include <utility>

namespace ykm {
namespace {

EventLoop* loop_from(JSContext* context) {
    auto* session = Session::from_context(context);
    return session ? &session->loop() : nullptr;
}

int64_t delay_arg(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 2) return 0;
    double value = 0;
    if (JS_ToFloat64(context, &value, argv[1]) < 0) return 0;
    if (value < 0) value = 0;
    if (value > 86400000) value = 86400000;
    return static_cast<int64_t>(value);
}

} // namespace

EventLoop::~EventLoop() {
    shutdown();
}

void EventLoop::attach(JSContext* context) {
    context_ = context;
}

void EventLoop::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_) {
        timers_.clear();
        completions_.clear();
        results_.clear();
        return;
    }
    for (Timer& timer : timers_) {
        JS_FreeValue(context_, timer.callback);
    }
    timers_.clear();
    for (Completion& completion : completions_) {
        JS_FreeValue(context_, completion.func);
        JS_FreeValue(context_, completion.argument);
    }
    completions_.clear();
    for (Result& result : results_) {
        JS_FreeValue(context_, result.func);
    }
    results_.clear();
}

uint32_t EventLoop::set_timer(JSValueConst callback, int64_t delay_ms, bool repeat) {
    Timer timer;
    timer.id = next_id_++;
    timer.callback = JS_DupValue(context_, callback);
    timer.interval = std::chrono::milliseconds(delay_ms);
    timer.when = Clock::now() + timer.interval;
    timer.repeat = repeat;
    timers_.push_back(timer);
    return timer.id;
}

void EventLoop::clear_timer(uint32_t id) {
    for (Timer& timer : timers_) {
        if (timer.id == id) timer.cancelled = true;
    }
}

void EventLoop::enqueue_call(JSValue func, JSValue argument) {
    std::lock_guard<std::mutex> lock(mutex_);
    completions_.push_back({func, argument});
}

void EventLoop::enqueue_result(JSValue func, std::string text, bool ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    results_.push_back({func, std::move(text), ok});
}

bool EventLoop::pump() {
    if (!context_) return false;
    bool did_work = false;
    const auto now = Clock::now();
    std::vector<Timer> due;
    for (Timer& timer : timers_) {
        if (!timer.cancelled && timer.when <= now) {
            due.push_back(timer);
            timer.callback = JS_DupValue(context_, timer.callback);
            if (timer.repeat) {
                timer.when = now + timer.interval;
            } else {
                timer.cancelled = true;
            }
        }
    }
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [&](Timer& timer) {
        if (!timer.cancelled) return false;
        JS_FreeValue(context_, timer.callback);
        return true;
    }), timers_.end());

    for (Timer& timer : due) {
        JSValue result = JS_Call(context_, timer.callback, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(result)) {
            if (auto* session = Session::from_context(context_)) {
                session->report_exception("timer");
            }
        }
        JS_FreeValue(context_, result);
        JS_FreeValue(context_, timer.callback);
        did_work = true;
    }

    std::vector<Result> result_batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_batch.swap(results_);
    }
    for (Result& item : result_batch) {
        JSValue value = JS_NewObject(context_);
        JS_SetPropertyStr(context_, value, "ok", JS_NewBool(context_, item.ok));
        JS_SetPropertyStr(context_, value, "log", JS_NewString(context_, item.text.c_str()));
        JSValue result = JS_Call(context_, item.func, JS_UNDEFINED, 1, &value);
        if (JS_IsException(result)) {
            if (auto* session = Session::from_context(context_)) session->report_exception("async");
        }
        JS_FreeValue(context_, result);
        JS_FreeValue(context_, item.func);
        JS_FreeValue(context_, value);
        did_work = true;
    }

    while (!completions_.empty()) {
        std::vector<Completion> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch.swap(completions_);
        }
        for (Completion& completion : batch) {
            JSValue argument = completion.argument;
            JSValue result = JS_Call(context_, completion.func, JS_UNDEFINED, 1, &argument);
            if (JS_IsException(result)) {
                if (auto* session = Session::from_context(context_)) {
                    session->report_exception("async");
                }
            }
            JS_FreeValue(context_, result);
            JS_FreeValue(context_, completion.func);
            JS_FreeValue(context_, completion.argument);
            did_work = true;
        }
    }
    return did_work;
}

bool EventLoop::has_work() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!completions_.empty() || !results_.empty()) return true;
    for (const Timer& timer : timers_) {
        if (!timer.cancelled) return true;
    }
    return false;
}

std::optional<EventLoop::Clock::duration> EventLoop::time_until_next() const {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!completions_.empty() || !results_.empty()) return Clock::duration::zero();
    }
    std::optional<Clock::time_point> next;
    for (const Timer& timer : timers_) {
        if (timer.cancelled) continue;
        if (!next || timer.when < *next) next = timer.when;
    }
    if (!next) return std::nullopt;
    const auto now = Clock::now();
    if (*next <= now) return Clock::duration::zero();
    return *next - now;
}

JSValue EventLoop::job_call(JSContext* context, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    return JS_Call(context, argv[0], JS_UNDEFINED, 0, nullptr);
}

JSValue EventLoop::js_set_timeout(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    EventLoop* loop = loop_from(context);
    if (!loop || argc < 1 || !JS_IsFunction(context, argv[0])) {
        return JS_ThrowTypeError(context, "setTimeout requires a function");
    }
    return JS_NewUint32(context, loop->set_timer(argv[0], delay_arg(context, argc, argv), false));
}

JSValue EventLoop::js_set_interval(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    EventLoop* loop = loop_from(context);
    if (!loop || argc < 1 || !JS_IsFunction(context, argv[0])) {
        return JS_ThrowTypeError(context, "setInterval requires a function");
    }
    return JS_NewUint32(context, loop->set_timer(argv[0], delay_arg(context, argc, argv), true));
}

JSValue EventLoop::js_clear_timer(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    EventLoop* loop = loop_from(context);
    if (!loop || argc < 1) return JS_UNDEFINED;
    uint32_t id = 0;
    if (JS_ToUint32(context, &id, argv[0]) == 0) loop->clear_timer(id);
    return JS_UNDEFINED;
}

JSValue EventLoop::js_queue_microtask(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(context, argv[0])) {
        return JS_ThrowTypeError(context, "queueMicrotask requires a function");
    }
    JSValue callback = JS_DupValue(context, argv[0]);
    if (JS_EnqueueJob(context, job_call, 1, &callback) < 0) {
        JS_FreeValue(context, callback);
        return JS_EXCEPTION;
    }
    JS_FreeValue(context, callback);
    return JS_UNDEFINED;
}

void event_loop_install_timers(JSContext* context) {
    JSValue global = JS_GetGlobalObject(context);
    JS_SetPropertyStr(context, global, "setTimeout", JS_NewCFunction(context, EventLoop::js_set_timeout, "setTimeout", 1));
    JS_SetPropertyStr(context, global, "setInterval", JS_NewCFunction(context, EventLoop::js_set_interval, "setInterval", 1));
    JS_SetPropertyStr(context, global, "clearTimeout", JS_NewCFunction(context, EventLoop::js_clear_timer, "clearTimeout", 1));
    JS_SetPropertyStr(context, global, "clearInterval", JS_NewCFunction(context, EventLoop::js_clear_timer, "clearInterval", 1));
    JS_SetPropertyStr(context, global, "queueMicrotask", JS_NewCFunction(context, EventLoop::js_queue_microtask, "queueMicrotask", 1));
    JS_FreeValue(context, global);
}

} // namespace ykm
