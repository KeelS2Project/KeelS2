#include <keels2/plugin.h>

extern "C" KeelBool KeelPlugin_Query(const KeelHostQuery*, KeelPluginInfo* info)
{
    if (!info || info->size != sizeof(KeelPluginInfo))
    {
        return KEEL_FALSE;
    }
    info->size = sizeof(KeelPluginInfo);
    info->abi_version = KEELS2_PLUGIN_ABI_VERSION + 1;
    info->name = "Invalid Test Plugin";
    info->version = "1";
    return KEEL_TRUE;
}

extern "C" KeelBool KeelPlugin_Load(const KeelHostApi*, KeelPluginHandle)
{
    return KEEL_FALSE;
}

extern "C" void KeelPlugin_Unload(KeelPluginHandle)
{
}
