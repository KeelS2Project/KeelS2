#include <keels2/plugin.h>
#include <cstdint>
#if defined(_MSC_VER)
#define DOCS_NOINLINE __declspec(noinline)
#else
#define DOCS_NOINLINE __attribute__((noinline))
#endif
extern "C" KEELS2_PLUGIN_EXPORT DOCS_NOINLINE std::int32_t DocsDouble(std::int32_t value)
{
    volatile std::int32_t input = value;
    return input * 2;
}
