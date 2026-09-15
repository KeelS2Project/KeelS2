#include <cstdint>

#if defined(_WIN32)
#define KEELHOOK_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define KEELHOOK_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#if defined(_MSC_VER)
#define KEELHOOK_TEST_NOINLINE __declspec(noinline)
#else
#define KEELHOOK_TEST_NOINLINE __attribute__((noinline))
#endif

KEELHOOK_TEST_EXPORT KEELHOOK_TEST_NOINLINE std::int32_t KeelHookBackendTarget(std::int32_t value)
{
    return value * 3 + 7;
}
