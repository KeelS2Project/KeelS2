#ifndef KEELS2_PLATFORM_DIAGNOSTIC_TRACE_H
#define KEELS2_PLATFORM_DIAGNOSTIC_TRACE_H

#include <string_view>

namespace keels2::platform
{

void AppendShutdownTrace(
    std::string_view event,
    std::string_view detail = {}) noexcept;

}

#endif
