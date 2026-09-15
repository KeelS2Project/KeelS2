#include <keels2/platform/dynamic_library.h>
#include <keels2/plugin.h>

#include <cstring>
#include <string>

namespace
{
unsigned resources{};
void Log(KeelPluginHandle, KeelLogLevel, const char*) {}
KeelResult Register(KeelPluginHandle, const KeelCommandSpec*, KeelCommandHandle*)
{
    ++resources;
    return KEEL_RESULT_UNSUPPORTED;
}
KeelResult Unregister(KeelPluginHandle, KeelCommandHandle)
{
    ++resources;
    return KEEL_RESULT_UNSUPPORTED;
}
KeelResult Query(KeelPluginHandle, const char*, std::uint32_t, const void**)
{
    ++resources;
    return KEEL_RESULT_NOT_FOUND;
}
}

int main(int count, char** arguments)
{
    if (count != 2)
    {
        return 1;
    }
    keels2::platform::DynamicLibrary library;
    std::string error;
    if (!library.Open(arguments[1], error))
    {
        return 2;
    }
    const auto query = reinterpret_cast<KeelPluginQueryFn>(library.Symbol("KeelPlugin_Query"));
    const auto load = reinterpret_cast<KeelPluginLoadFn>(library.Symbol("KeelPlugin_Load"));
    const auto unload = reinterpret_cast<KeelPluginUnloadFn>(library.Symbol("KeelPlugin_Unload"));
    const KeelHostQuery request{
        sizeof(KeelHostQuery), KEELS2_PLUGIN_ABI_VERSION, "1.0.0", "cs2",
#if defined(_WIN32)
        "win64"
#else
        "linuxsteamrt64"
#endif
    };
    KeelPluginInfo info{};
    info.size = sizeof(info);
    if (!query || !load || !unload || !query(&request, &info) ||
        !info.name || std::strcmp(info.name, "KeelS2 Stub") != 0 ||
        !info.version || std::strcmp(info.version, "1.0.0") != 0)
    {
        return 3;
    }
    const KeelHostApi api{
        sizeof(KeelHostApi), KEELS2_PLUGIN_ABI_VERSION,
        &Log, &Register, &Unregister, &Query
    };
    if (load(nullptr, 1) || load(&api, 0))
    {
        return 4;
    }
    for (unsigned cycle{}; cycle < 3; ++cycle)
    {
        if (!load(&api, 1) || load(&api, 1))
        {
            return 5;
        }
        unload(1);
        unload(1);
    }
    return resources == 0 ? 0 : 6;
}
