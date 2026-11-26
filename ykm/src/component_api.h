#pragma once

extern "C" {
#include <quickjs.h>
}

namespace ykm {

bool component_api_install(JSContext* context);

} // namespace ykm
