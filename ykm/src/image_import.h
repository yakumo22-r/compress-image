#pragma once

#include <string>
#include <filesystem>
#include <vector>

extern "C" {
#include <quickjs.h>
}

namespace ykm {
// Paths must come from the native shell dialog or a window drop, never JS.
JSValue import_images(JSContext* context, const std::vector<std::wstring>& paths);
void remember_image_source(const std::string& batch, const std::filesystem::path& directory,
                           std::vector<std::filesystem::path> files);
}
