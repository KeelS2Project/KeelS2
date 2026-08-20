#include <keels2/platform/console.h>

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace keels2::platform
{

void WriteEngineConsole(const char* message)
{
    if (!message)
    {
        return;
    }

    using MessageFunction = void (*)(const char*, ...);
    static const MessageFunction engine_message = [] {
#if defined(_WIN32)
        const HMODULE tier0 = GetModuleHandleW(L"tier0.dll");
        const FARPROC symbol = tier0 ? GetProcAddress(tier0, "Msg") : nullptr;
#else
        void* const tier0 = dlopen("libtier0.so", RTLD_NOW | RTLD_NOLOAD);
        void* const symbol = tier0 ? dlsym(tier0, "Msg") : nullptr;
        if (tier0)
        {
            dlclose(tier0);
        }
#endif
        static_assert(sizeof(symbol) == sizeof(MessageFunction));
        MessageFunction function{};
        std::memcpy(&function, &symbol, sizeof(function));
        return function;
    }();

    if (engine_message)
    {
        engine_message("%s", message);
        return;
    }

    std::fputs(message, stderr);
    std::fflush(stderr);
}

}
