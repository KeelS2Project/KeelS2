#include <keels2/plugin.h>

#include <string>

#ifndef KEELS2_DEPENDENCY_NAME
#define KEELS2_DEPENDENCY_NAME "Dependency Fixture"
#endif

#ifndef KEELS2_DEPENDENCY_VERSION
#define KEELS2_DEPENDENCY_VERSION "1.0.0"
#endif

#ifndef KEELS2_DEPENDENCY_HAS_MANIFEST
#define KEELS2_DEPENDENCY_HAS_MANIFEST 0
#endif

#ifndef KEELS2_DEPENDENCY_TARGET
#define KEELS2_DEPENDENCY_TARGET ""
#endif

#ifndef KEELS2_DEPENDENCY_TARGET_VERSION
#define KEELS2_DEPENDENCY_TARGET_VERSION ""
#endif

#ifndef KEELS2_DEPENDENCY_REQUIREMENT
#define KEELS2_DEPENDENCY_REQUIREMENT KEELS2_PLUGIN_DEPENDENCY_AT_LEAST
#endif

namespace
{

const KeelHostApi* g_host{};
KeelPluginHandle g_plugin{};

void Log(const char* action)
{
    if (g_host && g_host->log)
    {
        const std::string message = std::string("[Dependency Test] ") + action + " " +
            KEELS2_DEPENDENCY_NAME;
        g_host->log(g_plugin, KEEL_LOG_INFO, message.c_str());
    }
}

}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(
    const KeelHostQuery* query,
    KeelPluginInfo* info)
{
    if (!query || query->size != sizeof(KeelHostQuery) ||
        query->abi_version != KEELS2_PLUGIN_ABI_VERSION || !info ||
        info->size != sizeof(KeelPluginInfo))
    {
        return KEEL_FALSE;
    }
    *info = {
        sizeof(KeelPluginInfo),
        KEELS2_PLUGIN_ABI_VERSION,
        KEELS2_DEPENDENCY_NAME,
        "KeelS2 Tests",
        KEELS2_DEPENDENCY_VERSION,
        "Exercises deterministic dependency management"
    };
    return KEEL_TRUE;
}

#if KEELS2_DEPENDENCY_HAS_MANIFEST
extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Manifest(
    const KeelHostQuery* query,
    KeelPluginManifest* manifest)
{
    if (!query || query->size != sizeof(KeelHostQuery) ||
        query->abi_version != KEELS2_PLUGIN_ABI_VERSION || !manifest ||
        manifest->size != sizeof(KeelPluginManifest))
    {
        return KEEL_FALSE;
    }
    static const KeelPluginDependency dependency{
        sizeof(KeelPluginDependency),
        KEELS2_DEPENDENCY_REQUIREMENT,
        KEELS2_DEPENDENCY_TARGET,
        KEELS2_DEPENDENCY_TARGET_VERSION
    };
    *manifest = {
        sizeof(KeelPluginManifest),
        KEELS2_PLUGIN_MANIFEST_VERSION,
        1,
        0,
        &dependency
    };
    return KEEL_TRUE;
}
#endif

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(
    const KeelHostApi* api,
    KeelPluginHandle plugin)
{
    if (!api || api->size != sizeof(KeelHostApi) ||
        api->abi_version != KEELS2_PLUGIN_ABI_VERSION || !api->log || !plugin)
    {
        return KEEL_FALSE;
    }
    g_host = api;
    g_plugin = plugin;
    Log("load");
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin)
{
    if (plugin == g_plugin)
    {
        Log("unload");
    }
    g_host = nullptr;
    g_plugin = 0;
}
