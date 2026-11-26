#include "debug_console.h"

#include "session.h"

#include <algorithm>
#include <utility>

#include <imgui.h>

namespace ykm {
namespace {

ImVec4 level_color(LogLevel level) {
    switch (level) {
    case LogLevel::Error: return ImVec4(1.00f, 0.35f, 0.35f, 1.0f);
    case LogLevel::Command: return ImVec4(0.45f, 0.75f, 1.00f, 1.0f);
    case LogLevel::Result: return ImVec4(0.45f, 1.00f, 0.60f, 1.0f);
    case LogLevel::Info: return ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

} // namespace

void DebugConsole::pull_logs() {
#ifdef YKM_DEBUG_CONSOLE
    std::vector<std::pair<LogLevel, std::string>> pending = debug_take_logs();
    for (auto& entry : pending) {
        lines_.push_back({entry.first, std::move(entry.second)});
    }
    if (!pending.empty()) scroll_to_bottom_ = true;
    constexpr size_t kMaxLines = 4000;
    if (lines_.size() > kMaxLines) {
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - kMaxLines));
    }
#endif
}

void DebugConsole::submit(SessionManager& sessions) {
#ifdef YKM_DEBUG_CONSOLE
    std::string command(input_);
    input_[0] = '\0';
    if (command.empty()) return;
    debug_log(LogLevel::Command, "> " + command);
    sessions.execute_console(command);
#else
    (void)sessions;
#endif
}

void DebugConsole::render(SessionManager& sessions) {
#ifdef YKM_DEBUG_CONSOLE
    pull_logs();
    if (!open_) return;
    ImGui::SetNextWindowSize(ImVec2(720, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("YKM Debug Console", &open_)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Clear")) lines_.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("CLI role has priority; otherwise evaluates JS in main session");
    ImGui::Separator();

    const float footer = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##console-log", ImVec2(0, -footer), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const Line& line : lines_) {
            ImGui::PushStyleColor(ImGuiCol_Text, level_color(line.level));
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
        }
        if (scroll_to_bottom_) {
            ImGui::SetScrollHereY(1.0f);
            scroll_to_bottom_ = false;
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##console-input", input_, sizeof(input_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        submit(sessions);
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
#else
    (void)sessions;
#endif
}

} // namespace ykm
