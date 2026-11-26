#pragma once

namespace ykm {

class Session;

struct ApiUnit {
    const char* name;
    bool (*install)(Session& session);
};

} // namespace ykm
