#pragma once

#include <cstdint>

namespace ykm {

enum class RelationType : int32_t {
    Width = 1,
    Height,
    Size,
    LeftLeft,
    LeftCenter,
    LeftRight,
    CenterCenter,
    RightLeft,
    RightRight,
    TopTop,
    TopMiddle,
    TopBottom,
    MiddleMiddle,
    BottomTop,
    BottomBottom,
};

} // namespace ykm
