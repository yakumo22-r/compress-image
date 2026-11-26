#pragma once

#include <filesystem>

namespace ykm {

void load_embedded_permissions();
void load_disk_permissions(const std::filesystem::path& root);
bool app_allows(const char* permission);

} // namespace ykm
