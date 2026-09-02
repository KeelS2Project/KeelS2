#include <keels2/plugin.h>

static const KeelHostApi* host_api;
static KeelPluginHandle plugin_handle;

KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(
    const KeelHostQuery* query,
    KeelPluginInfo* info)
{
    if (!query || query->size != sizeof(KeelHostQuery) ||
        query->abi_version != KEELS2_PLUGIN_ABI_VERSION || !info ||
        info->size != sizeof(KeelPluginInfo))
    {
        return KEEL_FALSE;
    }
    info->size = sizeof(KeelPluginInfo);
    info->abi_version = KEELS2_PLUGIN_ABI_VERSION;
    info->name = "KeelS2 ABI 4 Fixture";
    info->author = "KeelS2 Tests";
    info->version = "0.8.0";
    info->description = "Frozen ABI 4 compatibility fixture";
    return KEEL_TRUE;
}

KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(
    const KeelHostApi* api,
    KeelPluginHandle plugin)
{
    if (!api || api->size != sizeof(KeelHostApi) ||
        api->abi_version != KEELS2_PLUGIN_ABI_VERSION || !api->log ||
        !api->register_command || !api->unregister_command ||
        !api->query_service || !plugin)
    {
        return KEEL_FALSE;
    }
    host_api = api;
    plugin_handle = plugin;
    api->log(plugin, KEEL_LOG_INFO, "frozen ABI 4 fixture loaded");
    return KEEL_TRUE;
}

KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin)
{
    if (host_api && plugin == plugin_handle)
    {
        host_api->log(plugin, KEEL_LOG_INFO, "frozen ABI 4 fixture unloaded");
    }
    host_api = 0;
    plugin_handle = 0;
}
