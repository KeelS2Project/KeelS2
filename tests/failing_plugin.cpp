#include <keels2/plugin.h>

namespace
{

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
    info->name = "Failing Test Plugin";
    info->version = "1";
    return KEEL_TRUE;
}

extern "C" KeelBool KeelPlugin_Load(const KeelHostApi* api, KeelPluginHandle plugin)
{
    if (!api || !api->log || !api->register_command)
    {
        return KEEL_FALSE;
    }
    api->log(plugin, KEEL_LOG_WARNING, "intentional warning severity probe");
    api->log(plugin, KEEL_LOG_ERROR, "intentional error severity probe");
    const KeelCommandSpec command{
        sizeof(KeelCommandSpec),
        "partial_failure",
        "Test resource cleanup after plugin load failure",
        0,
        &TestCommand,
        nullptr
    };
    KeelCommandHandle handle{};
    static_cast<void>(api->register_command(plugin, &command, &handle));
    return KEEL_FALSE;
}

extern "C" void KeelPlugin_Unload(KeelPluginHandle)
{
}
