#pragma once
#include "system_window.h"

// Internal backend factories. Application/UI code uses create_system_window().
namespace ykm {
std::unique_ptr<SystemWindow> create_native_window(const SystemWindowOptions& options);
std::unique_ptr<SystemWindow> create_sdl_window(const SystemWindowOptions& options);
}
