#include "window_events.h"

#include "system_window.h"
#include "session.h"

#include <algorithm>
#include <string>
#include "image_import.h"
#include "app_storage.h"
#include <chrono>
#include <cwctype>

namespace ykm {
namespace {

constexpr char kEventTargetScript[] = R"WEVTS(
(function () {
  var listeners = {};
  var handlers = {};

  function Event(type, init) {
    if (type === undefined) throw new TypeError("Event type is required");
    if (!init) init = {};
    this.type = String(type);
    this.bubbles = Boolean(init.bubbles);
    this.cancelable = Boolean(init.cancelable);
    this.composed = Boolean(init.composed);
    this.defaultPrevented = false;
    this.eventPhase = 2;
    this.isTrusted = false;
    this.target = null;
    this.currentTarget = null;
    this.timeStamp = Date.now();
    this._stopped = false;
    this._immediateStopped = false;
  }
  Event.prototype.preventDefault = function () {
    if (this.cancelable) this.defaultPrevented = true;
  };
  Event.prototype.stopPropagation = function () { this._stopped = true; };
  Event.prototype.stopImmediatePropagation = function () {
    this._stopped = this._immediateStopped = true;
  };

  function UIEvent(type, init) {
    Event.call(this, type, init);
    if (!init) init = {};
    for (var key in init) {
      if (Object.prototype.hasOwnProperty.call(init, key)) this[key] = init[key];
    }
    this.view = globalThis;
    this.detail = init.detail || 0;
  }
  UIEvent.prototype = Object.create(Event.prototype);
  UIEvent.prototype.constructor = UIEvent;

  function KeyboardEvent(type, init) { UIEvent.call(this, type, init); }
  KeyboardEvent.prototype = Object.create(UIEvent.prototype);
  KeyboardEvent.prototype.constructor = KeyboardEvent;

  function MouseEvent(type, init) { UIEvent.call(this, type, init); }
  MouseEvent.prototype = Object.create(UIEvent.prototype);
  MouseEvent.prototype.constructor = MouseEvent;

  function WheelEvent(type, init) { MouseEvent.call(this, type, init); }
  WheelEvent.prototype = Object.create(MouseEvent.prototype);
  WheelEvent.prototype.constructor = WheelEvent;

  function addEventListener(type, callback, options) {
    if (callback == null) return;
    var name = String(type);
    var capture = typeof options === "boolean" ? options : Boolean(options && options.capture);
    var once = Boolean(options && typeof options === "object" && options.once);
    var entries = listeners[name] || [];
    for (var i = 0; i < entries.length; i++) {
      if (entries[i].callback === callback && entries[i].capture === capture) return;
    }
    entries.push({ callback: callback, capture: capture, once: once });
    listeners[name] = entries;
  }

  function removeEventListener(type, callback, options) {
    var entries = listeners[String(type)];
    if (!entries) return;
    var capture = typeof options === "boolean" ? options : Boolean(options && options.capture);
    for (var i = 0; i < entries.length; i++) {
      if (entries[i].callback === callback && entries[i].capture === capture) {
        entries.splice(i, 1);
        return;
      }
    }
  }

  function dispatchEvent(event) {
    if (!(event instanceof Event)) throw new TypeError("dispatchEvent requires an Event");
    event.target = globalThis;
    event.currentTarget = globalThis;
    event.eventPhase = 2;
    event._stopped = false;
    event._immediateStopped = false;
    var entries = (listeners[event.type] || []).slice();
    for (var i = 0; i < entries.length; i++) {
      var entry = entries[i];
      if (entry.once) removeEventListener(event.type, entry.callback, entry.capture);
      if (typeof entry.callback === "function") entry.callback.call(globalThis, event);
      else if (entry.callback && typeof entry.callback.handleEvent === "function") entry.callback.handleEvent(event);
      if (event._immediateStopped) break;
    }
    if (!event._immediateStopped) {
      var handler = handlers[event.type];
      if (typeof handler === "function") handler.call(globalThis, event);
    }
    event.currentTarget = null;
    event.eventPhase = 0;
    return !event.defaultPrevented;
  }

  Object.defineProperty(globalThis, "window", { value: globalThis, configurable: true });
  Object.defineProperty(globalThis, "self", { value: globalThis, configurable: true });
  Object.defineProperty(globalThis, "Event", { value: Event, configurable: true });
  Object.defineProperty(globalThis, "UIEvent", { value: UIEvent, configurable: true });
  Object.defineProperty(globalThis, "KeyboardEvent", { value: KeyboardEvent, configurable: true });
  Object.defineProperty(globalThis, "MouseEvent", { value: MouseEvent, configurable: true });
  Object.defineProperty(globalThis, "WheelEvent", { value: WheelEvent, configurable: true });
  Object.defineProperty(globalThis, "addEventListener", { value: addEventListener, configurable: true });
  Object.defineProperty(globalThis, "removeEventListener", { value: removeEventListener, configurable: true });
  Object.defineProperty(globalThis, "dispatchEvent", { value: dispatchEvent, configurable: true });

  var types = ["keydown", "keyup", "textinput", "mousemove", "mousedown", "mouseup", "click", "dblclick", "contextmenu", "wheel"];
  for (var t = 0; t < types.length; t++) {
    (function (type) {
      Object.defineProperty(globalThis, "on" + type, {
        configurable: true,
        get: function () { return handlers[type] || null; },
        set: function (value) { handlers[type] = typeof value === "function" ? value : null; }
      });
    })(types[t]);
  }
  Object.defineProperty(globalThis, "__ykmDispatchInput", {
    value: function (type, init, constructorName) {
      var Constructor = globalThis[constructorName] || Event;
      var event = new Constructor(type, init);
      event.isTrusted = true;
      return dispatchEvent(event);
    }
  });
})();
)WEVTS";

void set_bool(JSContext* context, JSValue object, const char* name, bool value) {
    JS_SetPropertyStr(context, object, name, JS_NewBool(context, value));
}

void set_number(JSContext* context, JSValue object, const char* name, double value) {
    JS_SetPropertyStr(context, object, name, JS_NewFloat64(context, value));
}

void set_string(JSContext* context, JSValue object, const char* name, const std::string& value) {
    JS_SetPropertyStr(context, object, name, JS_NewStringLen(context, value.data(), value.size()));
}

std::string browser_key(uint32_t key) {
    switch (key) {
        case 8: return "Backspace"; case 9: return "Tab"; case 13: return "Enter";
        case 27: return "Escape"; case 32: return " "; case 33: return "PageUp";
        case 34: return "PageDown"; case 35: return "End"; case 36: return "Home";
        case 37: return "ArrowLeft"; case 38: return "ArrowUp"; case 39: return "ArrowRight";
        case 40: return "ArrowDown"; case 45: return "Insert"; case 46: return "Delete";
        case 20: return "CapsLock"; case 144: return "NumLock"; case 145: return "ScrollLock";
        case 0xA0: case 0xA1: return "Shift";
        case 0xA2: case 0xA3: return "Control";
        case 0xA4: case 0xA5: return "Alt";
        default: break;
    }
    if (key >= 'A' && key <= 'Z') return std::string(1, static_cast<char>(key - 'A' + 'a'));
    if (key >= '0' && key <= '9') return std::string(1, static_cast<char>(key));
    return "Unidentified";
}

std::string browser_code(uint32_t code) {
    switch (code) {
        case 0xA0: return "ShiftLeft"; case 0xA1: return "ShiftRight";
        case 0xA2: return "ControlLeft"; case 0xA3: return "ControlRight";
        case 0xA4: return "AltLeft"; case 0xA5: return "AltRight";
        default: break;
    }
    switch (code) {
        case 0x01: return "Escape";
        case 0x0F: return "Tab";
        case 0x1C: return "Enter";
        case 0x1D: return "ControlLeft";
        case 0x2A: return "ShiftLeft";
        case 0x36: return "ShiftRight";
        case 0x38: return "AltLeft";
        case 0x39: return "Space";
        case 0x0E: return "Backspace";
        case 0x47: return "Home";
        case 0x48: return "ArrowUp";
        case 0x49: return "PageUp";
        case 0x4B: return "ArrowLeft";
        case 0x4D: return "ArrowRight";
        case 0x4F: return "End";
        case 0x50: return "ArrowDown";
        case 0x51: return "PageDown";
        case 0x52: return "Insert";
        case 0x53: return "Delete";
        default: break;
    }
    if (code >= 0x10 && code <= 0x19) return "Digit" + std::string(1, static_cast<char>('0' + code - 0x10));
    if (code >= 0x1E && code <= 0x2C) {
        static constexpr char keys[] = "QWERTYUIOPASDFGHJKLZXCVBNM";
        return "Key" + std::string(1, keys[code - 0x1E]);
    }
    switch (code) {
        case 0x0B: return "Digit0";
        default: break;
    }
    return "Unidentified";
}
void set_modifiers(JSContext* context, JSValue init, uint32_t modifiers) {
    set_bool(context, init, "altKey", (modifiers & 1) != 0);
    set_bool(context, init, "ctrlKey", (modifiers & 2) != 0);
    set_bool(context, init, "metaKey", (modifiers & 4) != 0);
    set_bool(context, init, "shiftKey", (modifiers & 8) != 0);
}

void canvas_position(const InputViewport& viewport, float x, float y, double& client_x, double& client_y) {
    const float scale = std::max(0.01f, viewport.scale);
    client_x = (x - viewport.x) / scale;
    client_y = (y - viewport.y) / scale;
}

bool call_dispatch(Session& session, const char* type, const char* constructor_name, JSValue init) {
    JSContext* context = session.context();
    JSValue global = JS_GetGlobalObject(context);
    JSValue dispatch = JS_GetPropertyStr(context, global, "__ykmDispatchInput");
    JSValue args[] = {JS_NewString(context, type), init, JS_NewString(context, constructor_name)};
    JSValue result = JS_Call(context, dispatch, global, 3, args);
    const bool handled = !JS_IsException(result) && JS_ToBool(context, result) == 0;
    if (JS_IsException(result)) session.report_exception(type);
    JS_FreeValue(context, result);
    JS_FreeValue(context, args[0]);
    JS_FreeValue(context, args[1]);
    JS_FreeValue(context, args[2]);
    JS_FreeValue(context, dispatch);
    JS_FreeValue(context, global);
    session.drain_jobs();
    return handled;
}

JSValue mouse_init(JSContext* context, const SystemWindow& window, const InputViewport& viewport,
                   float x, float y, float movement_x,
                            float movement_y, int button, int buttons, int detail, uint32_t modifiers) {
    JSValue init = JS_NewObject(context);
    double client_x = 0;
    double client_y = 0;
    canvas_position(viewport, x, y, client_x, client_y);
    int window_x = 0;
    int window_y = 0;
    window.position(window_x, window_y);
    set_bool(context, init, "bubbles", true);
    set_bool(context, init, "cancelable", true);
    set_number(context, init, "clientX", client_x);
    set_number(context, init, "clientY", client_y);
    set_number(context, init, "pageX", client_x);
    set_number(context, init, "pageY", client_y);
    set_number(context, init, "offsetX", client_x);
    set_number(context, init, "offsetY", client_y);
    set_number(context, init, "screenX", window_x + x);
    set_number(context, init, "screenY", window_y + y);
    set_number(context, init, "movementX", movement_x / std::max(0.01f, viewport.scale));
    set_number(context, init, "movementY", movement_y / std::max(0.01f, viewport.scale));
    set_number(context, init, "button", button);
    set_number(context, init, "buttons", buttons);
    set_number(context, init, "detail", detail);
    set_modifiers(context, init, modifiers);
    return init;
}

} // namespace

bool window_events_install(Session& session) {
    JSContext* context = session.context();
    JSValue result = JS_Eval(context, kEventTargetScript, sizeof(kEventTargetScript) - 1,
                             "<window-events>", JS_EVAL_TYPE_GLOBAL);
    const bool ok = !JS_IsException(result);
    JS_FreeValue(context, result);
    return ok;
}

JSValue import_images(JSContext* context, const std::vector<std::wstring>& paths) {
        namespace fs = std::filesystem;
        static uint64_t sequence = 0;
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const std::string batch = "drop-" + std::to_string(stamp) + "-" + std::to_string(++sequence);
        const auto target = app_data_root() / batch;
        unsigned imported = 0, skipped = 0, visited = 0;
        uintmax_t bytes = 0;
        std::error_code ec;
        fs::path output_base;
        std::vector<fs::path> source_files;
        const bool created = fs::create_directories(target, ec);
        auto import = [&](const fs::path& path) {
            ++visited;
            std::error_code error;
            auto ext = path.extension().wstring();
            for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
            if ((ext != L".png" && ext != L".jpg" && ext != L".jpeg" && ext != L".webp") ||
                !fs::is_regular_file(fs::symlink_status(path, error))) { ++skipped; return; }
            const auto size = fs::file_size(path, error);
            if (error || size > 24u * 1024 * 1024 || bytes + size > 256u * 1024 * 1024) { ++skipped; return; }
            auto destination = target / path.filename();
            if (fs::exists(destination, error)) {
                destination = target / (std::to_wstring(visited) + L"-" + path.filename().wstring());
            }
            if (fs::copy_file(path, destination, fs::copy_options::none, error)) {
                ++imported;
                bytes += size;
                source_files.push_back(path);
            }
            else ++skipped;
        };
        if (created) {
            for (const auto& name : paths) {
                if (visited >= 500) { ++skipped; break; }
                const fs::path path(name);
                if (!path.is_absolute()) { ++skipped; continue; }
                ec.clear();
                const auto before = imported;
                const bool directory = fs::is_directory(fs::symlink_status(path, ec));
                if (directory) {
                    fs::directory_iterator it(path, ec), end;
                    for (; !ec && it != end && visited < 500; it.increment(ec)) import(it->path());
                    if (ec || it != end) ++skipped;
                } else {
                    import(path);
                }
                if (output_base.empty() && imported > before) {
                    output_base = directory ? path : path.parent_path();
                }
            }
            if (!imported) fs::remove(target, ec);
            else remember_image_source(batch, output_base, std::move(source_files));
        }
        JSValue init = JS_NewObject(context);
        set_string(context, init, "directory", imported ? "data:/" + batch : std::string());
        set_number(context, init, "imported", imported);
        set_number(context, init, "skipped", skipped);
        JSValue sources = JS_NewArray(context);
        for (uint32_t i = 0; i < paths.size(); ++i) {
            const auto name = fs::path(paths[i]).u8string();
            JS_SetPropertyUint32(context, sources, i, JS_NewStringLen(context,
                reinterpret_cast<const char*>(name.data()), name.size()));
        }
        JS_SetPropertyStr(context, init, "sources", sources);
        return init;
}

bool window_events_dispatch(Session& session, const SystemWindow& window, const InputViewport& viewport,
                            const SystemEvent& event) {
    JSContext* context = session.context();
    if (event.type == SystemEventType::FileDrop) {
        if (!app_allows("desktop.filedrop") || !app_allows("fs.data")) return false;
        return call_dispatch(session, "filedrop", "UIEvent", import_images(context, event.dropped_paths));
    }
    if (event.type == SystemEventType::KeyDown || event.type == SystemEventType::KeyUp) {
        JSValue init = JS_NewObject(context);
        set_bool(context, init, "bubbles", true);
        set_bool(context, init, "cancelable", true);
        set_string(context, init, "key", browser_key(event.key));
        set_string(context, init, "code", browser_code(event.scancode));
        set_number(context, init, "location", 0);
        set_bool(context, init, "repeat", event.repeat);
        set_bool(context, init, "isComposing", false);
        set_number(context, init, "keyCode", static_cast<double>(event.key));
        set_number(context, init, "which", static_cast<double>(event.key));
        set_modifiers(context, init, event.modifiers);
        return call_dispatch(session, event.type == SystemEventType::KeyDown ? "keydown" : "keyup", "KeyboardEvent", init);
    }
    if (event.type == SystemEventType::TextInput) {
        JSValue init = JS_NewObject(context);
        set_bool(context, init, "bubbles", true);
        set_bool(context, init, "cancelable", false);
        set_number(context, init, "charCode", event.character);
        set_number(context, init, "which", event.character);
        set_number(context, init, "keyCode", event.character);
        set_bool(context, init, "isComposing", event.composing);
        return call_dispatch(session, "textinput", "UIEvent", init);
    }
    if (event.type == SystemEventType::MouseMove) {
        JSValue init = mouse_init(context, window, viewport, event.x, event.y, event.delta_x,
                                  event.delta_y, 0, event.mouse_buttons, 0, event.modifiers);
        return call_dispatch(session, "mousemove", "MouseEvent", init);
    }
    if (event.type == SystemEventType::MouseButtonDown || event.type == SystemEventType::MouseButtonUp) {
        const int button = event.button;
        int buttons = static_cast<int>(event.mouse_buttons);
        JSValue init = mouse_init(context, window, viewport, event.x, event.y, 0, 0, button, buttons, 1, event.modifiers);
        const bool handled = call_dispatch(session,
            event.type == SystemEventType::MouseButtonDown ? "mousedown" : "mouseup", "MouseEvent", init);
        if (event.type == SystemEventType::MouseButtonUp && event.button == 0) {
            JSValue click_init = mouse_init(context, window, viewport, event.x, event.y, 0, 0, button, buttons, 1, event.modifiers);
            call_dispatch(session, event.double_click ? "dblclick" : "click", "MouseEvent", click_init);
        } else if (event.type == SystemEventType::MouseButtonUp && event.button == 2) {
            JSValue context_init = mouse_init(context, window, viewport, event.x, event.y, 0, 0, button, buttons, 1, event.modifiers);
            call_dispatch(session, "contextmenu", "MouseEvent", context_init);
        }
        return handled;
    }
    if (event.type == SystemEventType::MouseWheel) {
        JSValue init = mouse_init(context, window, viewport, event.x, event.y, 0, 0, 0,
                                  event.mouse_buttons, 0, event.modifiers);
        set_number(context, init, "deltaX", event.delta_x * 100.0f);
        set_number(context, init, "deltaY", -event.delta_y * 100.0f);
        set_number(context, init, "deltaZ", 0);
        set_number(context, init, "deltaMode", 0);
        return call_dispatch(session, "wheel", "WheelEvent", init);
    }
    return false;
}

} // namespace ykm
