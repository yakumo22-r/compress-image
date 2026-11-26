#pragma once

#include "debug_log.h"

#include <string>
#include <string_view>
#include <vector>

namespace ykm {

class SessionManager;

class DebugConsole {
public:
    void render(SessionManager& sessions);
    void toggle() { open_ = !open_; }

private:
    struct Line {
        LogLevel level;
        std::string text;
    };

    void submit(SessionManager& sessions);
    void pull_logs();

    std::vector<Line> lines_;
    char input_[2048]{};
    bool open_ = false;
    bool scroll_to_bottom_ = false;
};

} // namespace ykm
