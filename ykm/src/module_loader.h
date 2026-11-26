#pragma once

#include <filesystem>
#include <string>

extern "C" {
#include <quickjs.h>
}

namespace ykm {

class ModuleLoader {
public:
    void install(JSRuntime* runtime, std::filesystem::path root);

private:
    static char* normalize(JSContext* context, const char* base_name, const char* name, void* opaque);
    static JSModuleDef* load(JSContext* context, const char* name, void* opaque);

    bool resolve_file(const std::string& name, std::filesystem::path& result) const;
    static bool embedded_source(const std::string& name, std::string& source);
    std::filesystem::path root_;
};

} // namespace ykm
