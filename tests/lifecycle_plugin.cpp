#include <keels2/plugin.h>

namespace
{

#if defined(KEELS2_TEST_PLUGIN_FIRST)
constexpr const char* kName = "Lifecycle First";
constexpr const char* kCommand = "lifecycle_first";
constexpr const char* kUnloadMessage = "first unload callback completed";
#else
constexpr const char* kName = "Lifecycle Second";
constexpr const char* kCommand = "lifecycle_second";
constexpr const char* kUnloadMessage = "second unload callback completed";
#endif

const KeelHostApi* g_api{};
KeelPluginHandle g_plugin{};

void TestCommand(const KeelCommandInvocation*, void*)
{
}

}

extern "C" KeelBool KeelPlugin_Query(const KeelHostQuery* query, KeelPluginInfo* info)
{
    if (!query || query->size != sizeof(KeelHostQuery) ||
        query->abi_version != KEELS2_PLUGIN_ABI_VERSION ||
        !info || info->size != sizeof(KeelPluginInfo))
    {
        return KEEL_FALSE;
    }
    info->size = sizeof(KeelPluginInfo);
    info->abi_version = KEELS2_PLUGIN_ABI_VERSION;
    info->name = kName;
    info->version = "1";
    return KEEL_TRUE;
}

extern "C" KeelBool KeelPlugin_Load(const KeelHostApi* api, KeelPluginHandle plugin)
{
    if (!api || api->size != sizeof(KeelHostApi) ||
        api->abi_version != KEELS2_PLUGIN_ABI_VERSION ||
        !api->log || !api->register_command || plugin == 0)
    {
        return KEEL_FALSE;
    }
    const KeelCommandSpec command{
        sizeof(KeelCommandSpec),
        kCommand,
        "Lifecycle ordering test",
        0,
        &TestCommand,
        nullptr
    };
    KeelCommandHandle handle{};
    if (api->register_command(plugin, &command, &handle) != KEEL_RESULT_OK)
    {
        return KEEL_FALSE;
    }
    g_api = api;
    g_plugin = plugin;
    return KEEL_TRUE;
}

extern "C" void KeelPlugin_Unload(KeelPluginHandle plugin)
{
    if (g_api && plugin == g_plugin)
    {
        g_api->log(plugin, KEEL_LOG_INFO, kUnloadMessage);
    }
    g_api = nullptr;
    g_plugin = 0;
}
