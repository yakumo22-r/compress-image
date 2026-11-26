#pragma once

extern "C" {
#include <quickjs.h>
}

namespace ykm {

void host_tools_install(JSContext* context, JSValue api);

} // namespace ykm
