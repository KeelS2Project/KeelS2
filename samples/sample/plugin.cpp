#include "plugin.h"

bool SamplePlugin::Load()
{
    gameFrameLogged = false;

    integer = CreateConVar<int>(
        "keels2_sample_int",
        42,
        "KeelS2 sample bounded integer",
        FCVAR_NOTIFY,
        0,
        100,
        &SamplePlugin::IntegerChanged);

    floating = CreateConVar<float>(
        "keels2_sample_float",
        1.25f,
        "KeelS2 sample bounded float",
        FCVAR_NONE,
        0.25f,
        4.0f);

    limitTeams = FindConVar<int>("mp_limitteams");

    const bool commandCreated = CreateCommand(
        "keel_sample",
        "Runs the KeelS2 Source 2 sample",
        &SamplePlugin::Command);

    const bool eventListening = ListenForGameEvent(
        "round_start",
        &SamplePlugin::OnRoundStart);

    if (!integer || !floating || !limitTeams ||
        !commandCreated || !eventListening)
    {
        LogError("registration failed");
        return false;
    }

    LogMessage(
        "ready command=keel_sample "
        "event=round_start convar=mp_limitteams");

    return true;
}

void SamplePlugin::Unload()
{
    LogMessage("unloaded; ordinary resources required no manual cleanup");
}

void SamplePlugin::OnLevelInit(
    KeyValues* keyValues,
    ILoopModePrerequisiteRegistry* prerequisiteRegistry)
{
    LogMessage(
        keyValues && prerequisiteRegistry
            ? "LevelInit context=complete"
            : "LevelInit context=partial");
}

void SamplePlugin::OnLevelShutdown()
{
    LogMessage("LevelShutdown");
}

bool SamplePlugin::OnClientConnect(
    CPlayerSlot slot,
    const char* name,
    uint64 xuid,
    const char* networkId,
    bool unknown,
    CBufferString*)
{
    LogMessage(
        "ClientConnect slot={} name={} xuid={} "
        "network_id={} unknown={} decision=accept",
        slot.Get(),
        name,
        xuid,
        networkId,
        unknown);

    return true;
}

bool SamplePlugin::OnClientCommand(
    CPlayerSlot slot,
    const CCommand& command)
{
    const char* verb = command.ArgC() > 0 ? command[0] : "";
    const char* argument = command.ArgC() > 1 ? command[1] : "";

    LogMessage(
        "ClientCommand slot={} verb={} argument={} decision=accept",
        slot.Get(),
        verb,
        argument);

    return true;
}

void SamplePlugin::OnGameFrame(
    bool simulating,
    bool firstTick,
    bool lastTick)
{
    if (gameFrameLogged)
    {
        return;
    }

    gameFrameLogged = true;

    LogMessage(
        "GameFrame simulating={} first_tick={} last_tick={}",
        simulating,
        firstTick,
        lastTick);
}

void SamplePlugin::OnClientConnected(
    CPlayerSlot slot,
    const char* name,
    uint64 xuid,
    const char* networkId,
    const char* address,
    bool fakePlayer)
{
    LogMessage(
        "ClientConnected slot={} name={} xuid={} "
        "network_id={} address={} fake={}",
        slot.Get(),
        name,
        xuid,
        networkId,
        address,
        fakePlayer);
}

void SamplePlugin::OnClientPutInServer(
    CPlayerSlot slot,
    const char* name,
    int clientType,
    uint64 xuid)
{
    LogMessage(
        "ClientPutInServer client_type={} slot={} name={} xuid={}",
        clientType,
        slot.Get(),
        name,
        xuid);
}

void SamplePlugin::OnClientActive(
    CPlayerSlot slot,
    bool loadGame,
    const char* name,
    uint64 xuid)
{
    LogMessage(
        "ClientActive load_game={} slot={} name={} xuid={}",
        loadGame,
        slot.Get(),
        name,
        xuid);
}

void SamplePlugin::OnClientFullyConnected(CPlayerSlot slot)
{
    LogMessage("ClientFullyConnected slot={}", slot.Get());
}

void SamplePlugin::OnClientDisconnecting(
    CPlayerSlot slot,
    ENetworkDisconnectionReason reason,
    const char* name,
    uint64 xuid,
    const char* networkId)
{
    LogMessage(
        "ClientDisconnecting slot={} name={} xuid={} "
        "network_id={} reason={}",
        slot.Get(),
        name,
        xuid,
        networkId,
        reason);
}

void SamplePlugin::OnClientSettingsChanged(CPlayerSlot slot)
{
    LogMessage("ClientSettingsChanged slot={}", slot.Get());
}

void SamplePlugin::OnAllPluginsLoaded()
{
    LogMessage("AllPluginsLoaded");
}

void SamplePlugin::Command(
    const CCommandContext& context,
    const CCommand& command)
{
    if (command.ArgC() > 2 ||
        (command.ArgC() == 2 && V_strcmp(command[1], "bump") != 0))
    {
        LogError("usage: keel_sample [bump]");
        return;
    }

    if (command.ArgC() == 2)
    {
        const int value = integer.Get();
        integer.Set(value == integer.Max() ? integer.Min() : value + 1);
        floating.Set(floating.Get() + 0.25f);
    }

    LogMessage(
        "caller={} int={} float={} mp_limitteams={}",
        context.GetPlayerSlot().Get(),
        integer.Get(),
        floating.Get(),
        limitTeams.Get());
}

void SamplePlugin::IntegerChanged(
    ConVar<int>& convar,
    CSplitScreenSlot slot,
    int newValue,
    int oldValue)
{
    LogMessage(
        "{} changed slot={} old={} new={}",
        convar.GetName(),
        slot.Get(),
        oldValue,
        newValue);
}

void SamplePlugin::OnRoundStart(IGameEvent*)
{
    LogMessage("event=round_start");
}

KEELS2_PLUGIN(SamplePlugin)
