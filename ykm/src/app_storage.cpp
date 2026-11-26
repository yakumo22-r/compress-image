#include "app_storage.h"

#include <ykm/embedded_app.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>

namespace ykm {
namespace {

std::string normalize_asset(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    std::filesystem::path path(name);
    name = path.lexically_normal().generic_string();
    if (name == ".") name.clear();
    return name;
}

std::string safe_id(std::string value) {
    if (value.empty()) return "ykm-app";
    for (char& c : value) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!safe) c = '_';
    }
    return value;
}

std::filesystem::path local_app_data() {
#ifdef _WIN32
    if (const char* value = std::getenv("LOCALAPPDATA")) return value;
#endif
    if (const char* value = std::getenv("HOME")) return std::filesystem::path(value) / ".local" / "share";
    return std::filesystem::temp_directory_path();
}

} // namespace

bool g_permissions_set = false;
std::vector<std::string> g_permissions;

void set_app_permissions(std::vector<std::string> permissions) {
    g_permissions = std::move(permissions);
    g_permissions_set = true;
}

void load_embedded_permissions() {
    const YkmEmbeddedApp* app = ykm_get_embedded_app();
    if (!app || app->struct_size < sizeof(YkmEmbeddedApp)) return;
    std::vector<std::string> permissions;
    for (uint32_t index = 0; index < app->permission_count; ++index) {
        if (app->permissions && app->permissions[index]) permissions.emplace_back(app->permissions[index]);
    }
    set_app_permissions(std::move(permissions));
}

void load_disk_permissions(const std::filesystem::path& root) {
    std::ifstream input(root / "ykm.permissions");
    if (!input) return;
    std::vector<std::string> permissions;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) permissions.push_back(line);
    }
    set_app_permissions(std::move(permissions));
}

bool app_allows(const char* permission) {
    if (!permission) return true;
    if (!g_permissions_set) return std::string_view(permission) != "desktop.filedrop";
    return std::find(g_permissions.begin(), g_permissions.end(), permission) != g_permissions.end();
}

std::string app_id() {
    const YkmEmbeddedApp* app = ykm_get_embedded_app();
    return safe_id(app && app->id ? app->id : "ykm-app");
}

std::filesystem::path app_data_root() {
    return local_app_data() / "YKM" / app_id() / "data";
}

std::filesystem::path app_cache_root() {
    return local_app_data() / "YKM" / app_id() / "cache";
}

bool embedded_asset(const std::string& name, AssetView& result) {
    const YkmEmbeddedApp* app = ykm_get_embedded_app();
    if (!app || app->struct_size < sizeof(YkmEmbeddedApp)) return false;
    const std::string normalized = normalize_asset(name);
    for (uint32_t index = 0; index < app->asset_count; ++index) {
        const YkmEmbeddedAsset& asset = app->assets[index];
        if (asset.name && normalized == asset.name) {
            result = {asset.data, asset.size};
            return true;
        }
    }
    return false;
}

bool embedded_asset_exists(const std::string& name) {
    AssetView view;
    if (embedded_asset(name, view)) return true;
    return !embedded_asset_directory(name).empty();
}

std::vector<std::string> embedded_asset_directory(const std::string& directory) {
    const YkmEmbeddedApp* app = ykm_get_embedded_app();
    if (!app || app->struct_size < sizeof(YkmEmbeddedApp)) return {};
    std::string prefix = normalize_asset(directory);
    if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
    std::set<std::string> names;
    for (uint32_t index = 0; index < app->asset_count; ++index) {
        const std::string name = app->assets[index].name ? app->assets[index].name : "";
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string rest = name.substr(prefix.size());
        if (rest.empty()) continue;
        names.insert(rest.substr(0, rest.find('/')));
    }
    return {names.begin(), names.end()};
}

} // namespace ykm
