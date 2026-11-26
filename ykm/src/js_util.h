#pragma once

#include <string>
#include <string_view>

extern "C" {
#include <quickjs.h>
}

namespace ykm {

std::string js_to_string(JSContext* context, JSValueConst value);
std::string js_to_output(JSContext* context, JSValueConst value);
bool js_is_uint8_array(JSContext* context, JSValueConst value);
JSValue js_new_uint8_array(JSContext* context, const uint8_t* data, size_t size);
bool js_copy_bytes(JSContext* context, JSValueConst value, std::string& out);

} // namespace ykm
