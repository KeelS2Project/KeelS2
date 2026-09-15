#include <keels2/keelhook.h>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

#if defined(_MSC_VER)
#define DOCS_NOINLINE __declspec(noinline)
#else
#define DOCS_NOINLINE __attribute__((noinline))
#endif

namespace
{
std::array<char, KEELHOOK_VAFMT_BUFFER_SIZE> output;
}

extern "C" KEELS2_PLUGIN_EXPORT DOCS_NOINLINE std::int32_t DocsFormat(const char* format, ...)
{
    output.fill('\0');
    std::va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(output.data(), output.size(), format, arguments);
    va_end(arguments);
    return written;
}

extern "C" KEELS2_PLUGIN_EXPORT const char* DocsFormattedText()
{
    return output.data();
}
