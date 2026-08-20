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
    info->name = "Reserved Command Test";
    info->author = "KeelS2 Project";
    info->version = "1";
    info->description = "Verifies that Core owns the keel command";
    return KEEL_TRUE;
}

extern "C" KeelBool KeelPlugin_Load(const KeelHostApi* api, KeelPluginHandle plugin)
{
    if (!api || !api->log || !api->register_command)
    {
        return KEEL_FALSE;
    }
    const KeelCommandSpec command{
        sizeof(KeelCommandSpec),
        "keel",
        "Must be rejected",
        0,
        &TestCommand,
        nullptr
    };
    KeelCommandHandle handle{};
    if (api->register_command(plugin, &command, &handle) != KEEL_RESULT_RESERVED_NAME)
    {
        return KEEL_FALSE;
    }
    const KeelCommandSpec mixed_case_command{
        sizeof(KeelCommandSpec),
        "KeEl",
        "Must also be rejected",
        0,
        &TestCommand,
        nullptr
    };
    if (api->register_command(plugin, &mixed_case_command, &handle) != KEEL_RESULT_RESERVED_NAME)
    {
        return KEEL_FALSE;
    }
    api->log(plugin, KEEL_LOG_INFO, "reserved command rejection passed");
    return KEEL_TRUE;
}

extern "C" void KeelPlugin_Unload(KeelPluginHandle)
{
}
