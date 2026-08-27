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
        !HookVirtual<
            &IServerGameDLL::GameFrame,
            &HooksPlugin::GameFrameHook>(
                server,
                keels2::kh::Phase::Both) ||
        !HookVirtual<
            &IServerGameClients::ClientConnect,
            &HooksPlugin::ClientConnectHook>(
                clients,
                keels2::kh::Phase::Post) ||
        !HookVirtual<
            &IServerGameClients::ClientCommand,
            &HooksPlugin::ClientCommandHook>(clients) ||
        !HookVirtual<
            &IServerGameClients::ClientDisconnect,
            &HooksPlugin::ClientDisconnectHook>(clients))
    {
        LogError("typed hook registration failed");
        return false;
    }

    LogMessage(
        "ready hooks=GameFrame,ClientConnect,ClientCommand,ClientDisconnect");
    return true;
}

keels2::kh::Action HooksPlugin::GameFrameHook(
    keels2::kh::Call<void>& call,
    bool simulating,
    bool firstTick,
    bool lastTick)
{
    if (call.CurrentPhase() == keels2::kh::Phase::Pre)
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
        return keels2::kh::Action::Continue;
    }

    if (!gameFramePostLogged)
    {
        gameFramePostLogged = true;
        LogMessage(
            "GameFrame post original_called={}",
            call.OriginalCalled());
    }
    return keels2::kh::Action::Continue;
}

keels2::kh::Action HooksPlugin::ClientConnectHook(
    keels2::kh::Call<bool>& call,
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
            "unknown={} original_called={} accepted={}",
            slot.Get(),
            name,
            xuid,
            networkId,
            unknown,
            call.OriginalCalled(),
            call.Result().value_or(false));
    }
    return keels2::kh::Action::Continue;
}

void HooksPlugin::ClientCommandHook(
    CPlayerSlot slot,
    const CCommand& command)
{
    if (clientCommandLogged)
    {
        return;
    }
    clientCommandLogged = true;
    LogMessage(
        "ClientCommand pre slot={} verb={}",
        slot.Get(),
        command.ArgC() > 0 ? command[0] : "");
}

void HooksPlugin::ClientDisconnectHook(
    CPlayerSlot slot,
    ENetworkDisconnectionReason reason,
    const char* name,
    uint64 xuid,
    const char* networkId)
{
    if (clientDisconnectLogged)
    {
        return;
    }
    clientDisconnectLogged = true;
    LogMessage(
        "ClientDisconnect pre slot={} name={} xuid={} network_id={} reason={}",
        slot.Get(),
        name,
        xuid,
        networkId,
        reason);
}

KEELS2_PLUGIN(HooksPlugin)
