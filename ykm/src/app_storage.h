#pragma once

#include "app_permissions.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ykm {

struct AssetView {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

std::string app_id();
std::filesystem::path app_data_root();
std::filesystem::path app_cache_root();
bool embedded_asset(const std::string& name, AssetView& result);
bool embedded_asset_exists(const std::string& name);
std::vector<std::string> embedded_asset_directory(const std::string& directory);
} // namespace ykm
