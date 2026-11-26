#include "js_util.h"

#include <webp/decode.h>
#include <webp/encode.h>
#include <memory>

namespace ykm {
namespace {
constexpr size_t max_bytes = 24 * 1024 * 1024;
constexpr int max_pixels = 8000000;

struct ByteView {
    JSContext* js;
    JSValue buffer = JS_UNDEFINED;
    const uint8_t* data = nullptr;
    size_t size = 0;
    ~ByteView() { JS_FreeValue(js, buffer); }
    bool read(JSValueConst value) {
        if (JS_GetTypedArrayType(value) != JS_TYPED_ARRAY_UINT8) return false;
        size_t offset, element_size, capacity;
        buffer = JS_GetTypedArrayBuffer(js, value, &offset, &size, &element_size);
        if (JS_IsException(buffer)) return false;
        auto* base = JS_GetArrayBuffer(js, &capacity, buffer);
        if (!base || offset > capacity || size > capacity - offset) return false;
        data = base + offset;
        return true;
    }
};
using WebpBytes = std::unique_ptr<uint8_t, decltype(&WebPFree)>;

JSValue decode(JSContext* js, JSValueConst, int argc, JSValueConst* argv) {
    ByteView input{js};
    if (argc != 1 || !input.read(argv[0])) return JS_ThrowTypeError(js, "WebP requires a Uint8Array");
    if (input.size > max_bytes) return JS_ThrowRangeError(js, "WebP exceeds 24 MiB");
    WebPBitstreamFeatures info{};
    if (WebPGetFeatures(input.data, input.size, &info) != VP8_STATUS_OK)
        return JS_ThrowTypeError(js, "Invalid WebP image");
    if (info.has_animation) return JS_ThrowTypeError(js, "Animated WebP is not supported");
    if (info.width <= 0 || info.height <= 0 || info.width > max_pixels / info.height)
        return JS_ThrowRangeError(js, "WebP exceeds 8 million pixels");
    WebpBytes pixels(WebPDecodeRGBA(input.data, input.size, nullptr, nullptr), WebPFree);
    if (!pixels) return JS_ThrowInternalError(js, "WebP decoding failed");
    JSValue data = js_new_uint8_array(js, pixels.get(), size_t(info.width) * info.height * 4);
    if (JS_IsException(data)) return data;
    JSValue result = JS_NewObject(js);
    if (JS_IsException(result)) { JS_FreeValue(js, data); return result; }
    JS_SetPropertyStr(js, result, "width", JS_NewInt32(js, info.width));
    JS_SetPropertyStr(js, result, "height", JS_NewInt32(js, info.height));
    JS_SetPropertyStr(js, result, "data", data);
    return result;
}

JSValue encode(JSContext* js, JSValueConst, int argc, JSValueConst* argv) {
    int32_t width, height, quality;
    if (argc != 4 || !JS_IsNumber(argv[1]) || !JS_IsNumber(argv[2]) || !JS_IsNumber(argv[3]) ||
        JS_ToInt32(js, &width, argv[1]) || JS_ToInt32(js, &height, argv[2]) || JS_ToInt32(js, &quality, argv[3]))
        return JS_ThrowTypeError(js, "WebP requires RGBA bytes, width, height and quality");
    if (width <= 0 || height <= 0 || width > WEBP_MAX_DIMENSION || height > WEBP_MAX_DIMENSION ||
        width > max_pixels / height || quality < 1 || quality > 100)
        return JS_ThrowRangeError(js, "WebP dimensions or quality are out of range");
    ByteView input{js};
    if (!input.read(argv[0]) || input.size != size_t(width) * height * 4)
        return JS_ThrowTypeError(js, "Invalid WebP RGBA buffer");
    uint8_t* output = nullptr;
    const size_t size = WebPEncodeRGBA(input.data, width, height, width * 4, float(quality), &output);
    WebpBytes bytes(output, WebPFree);
    if (!size || !bytes) return JS_ThrowInternalError(js, "WebP encoding failed");
    return js_new_uint8_array(js, bytes.get(), size);
}
} // namespace

JSValue mod_webp_open(JSContext* js) {
    JSValue result = JS_NewObject(js);
    if (JS_IsException(result)) return result;
    JS_SetPropertyStr(js, result, "decode", JS_NewCFunction(js, decode, "decode", 1));
    JS_SetPropertyStr(js, result, "encode", JS_NewCFunction(js, encode, "encode", 4));
    return result;
}
} // namespace ykm
