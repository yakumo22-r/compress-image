#pragma once

#include <string>
#include <filesystem>
#include <utility>
#include <vector>

namespace ykm {

enum class LogLevel {
    Info,
    Error,
    Command,
    Result,
};

void debug_log(LogLevel level, std::string text);
std::vector<std::pair<LogLevel, std::string>> debug_take_logs();
bool debug_log_open_file(const std::filesystem::path& path, std::string& error);
void debug_log_close_file();
bool debug_log_file_enabled();
std::string debug_log_file_path();

} // namespace ykm
