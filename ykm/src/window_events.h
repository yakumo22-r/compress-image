#pragma once

#include "system_event.h"

namespace ykm {

class Session;
class SystemWindow;

struct InputViewport {
    float x = 0;
    float y = 0;
    float scale = 1;
};

bool window_events_install(Session& session);
bool window_events_dispatch(Session& session, const SystemWindow& window, const InputViewport& viewport,
                            const SystemEvent& event);

} // namespace ykm
