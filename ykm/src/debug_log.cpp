#include "debug_log.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace ykm {
namespace {

std::mutex g_log_mutex;
std::vector<std::pair<LogLevel, std::string>> g_pending_logs;
std::ofstream g_log_file;
std::filesystem::path g_log_path;

const char* level_name(LogLevel level) {
    switch (level) {
    case LogLevel::Info: return "INFO";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Command: return "COMMAND";
    case LogLevel::Result: return "RESULT";
    }
    return "INFO";
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

} // namespace

void debug_log(LogLevel level, std::string text) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
#ifdef YKM_DEBUG_CONSOLE
    g_pending_logs.emplace_back(level, text);
#endif
    if (g_log_file) {
        g_log_file << '[' << timestamp() << "] [" << level_name(level) << "] " << text << '\n';
        g_log_file.flush();
    }
}

std::vector<std::pair<LogLevel, std::string>> debug_take_logs() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::vector<std::pair<LogLevel, std::string>> result;
    result.swap(g_pending_logs);
    return result;
}

bool debug_log_open_file(const std::filesystem::path& path, std::string& error) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    if (ec) {
        error = "unable to create log directory: " + ec.message();
        return false;
    }
    std::ofstream next(path, std::ios::app | std::ios::binary);
    if (!next) {
        error = "unable to open log file";
        return false;
    }
    g_log_file = std::move(next);
    g_log_path = std::filesystem::absolute(path).lexically_normal();
    return true;
}

void debug_log_close_file() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_file.close();
    g_log_path.clear();
}

bool debug_log_file_enabled() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return g_log_file.is_open();
}

std::string debug_log_file_path() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return g_log_path.generic_string();
}

} // namespace ykm
