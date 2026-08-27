#include "plugin.h"

bool HooksPlugin::Load()
{
    gameFramePreLogged = false;
    gameFramePostLogged = false;
    clientConnectLogged = false;
    clientCommandLogged = false;
    clientDisconnectLogged = false;

    auto* server = GetSource2Server<IServerGameDLL>();
    auto* clients = GetSource2GameClients<IServerGameClients>();
    if (!server || !clients ||
        !HookPre(
            server,
            &IServerGameDLL::GameFrame,
            &HooksPlugin::GameFramePre) ||
        !HookPost(
            server,
            &IServerGameDLL::GameFrame,
            &HooksPlugin::GameFramePost) ||
        !HookPost(
            clients,
            &IServerGameClients::ClientConnect,
            &HooksPlugin::ClientConnectPost) ||
        !HookPre(
            clients,
            &IServerGameClients::ClientCommand,
            &HooksPlugin::ClientCommandPre) ||
        !HookPre(
            clients,
            &IServerGameClients::ClientDisconnect,
            &HooksPlugin::ClientDisconnectPre))
    {
        LogError("typed hook registration failed");
        return false;
    }

    LogMessage(
        "ready hooks=GameFrame,ClientConnect,ClientCommand,ClientDisconnect");
    return true;
}

PluginResult HooksPlugin::GameFramePre(
    bool simulating,
    bool firstTick,
    bool lastTick)
{
    if (!gameFramePreLogged)
    {
        gameFramePreLogged = true;
        LogMessage(
            "GameFrame pre simulating={} first_tick={} last_tick={}",
            simulating,
            firstTick,
            lastTick);
    }
    return plugin_continue;
}

PluginResult HooksPlugin::GameFramePost(bool, bool, bool)
{
    if (!gameFramePostLogged)
    {
        gameFramePostLogged = true;
        LogMessage("GameFrame post");
    }
    return plugin_continue;
}

PluginResult HooksPlugin::ClientConnectPost(
    CPlayerSlot slot,
    const char* name,
    uint64 xuid,
    const char* networkId,
    bool unknown,
    CBufferString*)
{
    if (!clientConnectLogged)
    {
        clientConnectLogged = true;
        LogMessage(
            "ClientConnect post slot={} name={} xuid={} network_id={} "
            "unknown={}",
            slot.Get(),
            name,
            xuid,
            networkId,
            unknown);
    }
    return plugin_continue;
}

PluginResult HooksPlugin::ClientCommandPre(
    CPlayerSlot slot,
    const CCommand& command)
{
    if (clientCommandLogged)
    {
        return plugin_continue;
    }
    clientCommandLogged = true;
    LogMessage(
        "ClientCommand pre slot={} verb={}",
        slot.Get(),
        command.ArgC() > 0 ? command[0] : "");
    return plugin_continue;
}

PluginResult HooksPlugin::ClientDisconnectPre(
    CPlayerSlot slot,
    ENetworkDisconnectionReason reason,
    const char* name,
    uint64 xuid,
    const char* networkId)
{
    if (clientDisconnectLogged)
    {
        return plugin_continue;
    }
    clientDisconnectLogged = true;
    LogMessage(
        "ClientDisconnect pre slot={} name={} xuid={} network_id={} reason={}",
        slot.Get(),
        name,
        xuid,
        networkId,
        reason);
    return plugin_continue;
}

KEELS2_PLUGIN(HooksPlugin)
