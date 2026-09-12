#include "plugin.h"

bool SamplePlugin::Load()
{
    auto* engine = GetEngineInterface<IVEngineServer2>(INTERFACEVERSION_VENGINESERVER);
    auto* server = GetSource2Server<IServerGameDLL>();
    auto* clients = GetSource2GameClients<IServerGameClients>();
    auto* cvars = GetCVarSystem<ICvar>();
    auto* network = GetEngineInterface<INetworkServerService>(
        NETWORKSERVERSERVICE_INTERFACE_VERSION);
    if (!engine || !server || !clients || !cvars || !network)
    {
        LogError("required Source 2 interfaces are unavailable");
        return false;
    }

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

    const KeelResult nativeAccess = limitTeams.WithNative([this](CConVarRef<int32>& typed) {
        const ConVarRefAbstract& untyped = typed;
        LogMessage("mp_limitteams typed={} untyped={}", typed.Get(), untyped.GetInt());
    });
    if (nativeAccess != KEEL_RESULT_OK)
    {
        LogError("native ConVar access service version 1 is required: {}", nativeAccess);
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

void SamplePlugin::OnGameFrame(bool, bool, bool)
{
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
    if (command.ArgC() == 2 && V_strcmp(command[1], "player") == 0)
    {
        DescribePlayer(context.GetPlayerSlot());
        return;
    }
    if (command.ArgC() > 2 ||
        (command.ArgC() == 2 && V_strcmp(command[1], "bump") != 0))
    {
        LogError("usage: keel_sample [bump|player]");
        return;
    }

    if (command.ArgC() == 2)
    {
        const KeelResult integerSet = integer.WithNative([this](CConVarRef<int32>& native) {
            const int32 next = native.Get() == integer.Max() ? integer.Min() : native.Get() + 1;
            native.Set(next);
        });
        const KeelResult floatingSet = floating.WithNative([](CConVarRef<float>& native) {
            native.Set(native.Get() + 0.25f);
        });
        if (integerSet != KEEL_RESULT_OK || floatingSet != KEEL_RESULT_OK)
        {
            LogError("sample ConVar update was rejected");
        }
    }

    LogMessage(
        "caller={} int={} float={} mp_limitteams={}",
        context.GetPlayerSlot().Get(),
        integer.Get(),
        floating.Get(),
        limitTeams.Get());
}

void SamplePlugin::DescribePlayer(CPlayerSlot slot)
{
    PlayerInfo player;
    if (!GetPlayer(slot, player))
    {
        LogError("keel_sample player requires a current connected client");
        return;
    }
    CUtlString text;
    text.Format("#%d %s | team=%d authenticated=%d\n", player.user_id,
        player.name.Get(), player.team, player.authenticated ? 1 : 0);
    const KeelResult console = PrintToConsole(player.slot, text.Get());
    if (console != KEEL_RESULT_OK)
    {
        LogError("player console output is unavailable: {}", console);
    }
    if (PrintToChat(player.slot, "Player details printed to your console.") != KEEL_RESULT_OK)
    {
        LogError("player chat output is unavailable");
    }
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
