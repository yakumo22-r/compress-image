#include "js_util.h"

namespace ykm {

std::string js_to_string(JSContext* context, JSValueConst value) {
    const char* text = JS_ToCString(context, value);
    if (!text) return {};
    std::string result(text);
    JS_FreeCString(context, text);
    return result;
}

std::string js_to_output(JSContext* context, JSValueConst value) {
    if (JS_IsObject(value)) {
        JSValue json = JS_JSONStringify(context, value, JS_UNDEFINED, JS_UNDEFINED);
        if (!JS_IsException(json) && !JS_IsUndefined(json)) {
            std::string result = js_to_string(context, json);
            JS_FreeValue(context, json);
            return result;
        }
        JS_FreeValue(context, json);
    }
    return js_to_string(context, value);
}

bool js_is_uint8_array(JSContext* context, JSValueConst value) {
    JSValue global = JS_GetGlobalObject(context);
    JSValue ctor = JS_GetPropertyStr(context, global, "Uint8Array");
    const int ok = JS_IsInstanceOf(context, value, ctor);
    JS_FreeValue(context, ctor);
    JS_FreeValue(context, global);
    return ok == 1;
}

JSValue js_new_uint8_array(JSContext* context, const uint8_t* data, size_t size) {
    JSValue buffer = JS_NewArrayBufferCopy(context, data, size);
    if (JS_IsException(buffer)) return buffer;
    JSValue global = JS_GetGlobalObject(context);
    JSValue ctor = JS_GetPropertyStr(context, global, "Uint8Array");
    JSValue result = JS_CallConstructor(context, ctor, 1, &buffer);
    JS_FreeValue(context, ctor);
    JS_FreeValue(context, global);
    JS_FreeValue(context, buffer);
    return result;
}

bool js_copy_bytes(JSContext* context, JSValueConst value, std::string& out) {
    if (JS_IsString(value)) {
        out = js_to_string(context, value);
        return true;
    }
    size_t size = 0;
    uint8_t* pointer = JS_GetArrayBuffer(context, &size, value);
    if (pointer) {
        out.assign(reinterpret_cast<char*>(pointer), size);
        return true;
    }
    if (js_is_uint8_array(context, value)) {
        JSValue buffer = JS_GetPropertyStr(context, value, "buffer");
        JSValue byte_offset = JS_GetPropertyStr(context, value, "byteOffset");
        JSValue byte_length = JS_GetPropertyStr(context, value, "byteLength");
        int64_t offset = 0;
        int64_t length = 0;
        JS_ToInt64(context, &offset, byte_offset);
        JS_ToInt64(context, &length, byte_length);
        size_t buffer_size = 0;
        uint8_t* data = JS_GetArrayBuffer(context, &buffer_size, buffer);
        JS_FreeValue(context, buffer);
        JS_FreeValue(context, byte_offset);
        JS_FreeValue(context, byte_length);
        if (!data || offset < 0 || length < 0 || static_cast<size_t>(offset + length) > buffer_size) {
            return false;
        }
        out.assign(reinterpret_cast<char*>(data + offset), static_cast<size_t>(length));
        return true;
    }
    return false;
}

} // namespace ykm
