#pragma once

#include <cstdint>

namespace ykm {

enum class WindowLayer : int32_t {
    BG = 0,
    Page = 1,
    Modal = 2,
    System = 3,
    Debug = 4,
};

constexpr int32_t kWindowLayerCount = 5;

} // namespace ykm
