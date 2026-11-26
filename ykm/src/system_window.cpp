#include "system_window_backend.h"

namespace ykm {
std::unique_ptr<SystemWindow> create_system_window(const SystemWindowOptions& options) {
#ifdef _WIN32
    return create_native_window(options);
#else
    // Compatibility backend only; native Apple/mobile backends are not implemented yet.
    return create_sdl_window(options);
#endif
}
}
