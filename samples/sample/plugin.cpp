#include "plugin.h"

namespace sample
{
bool SamplePlugin::Load()
{
    integer = CreateConVar<int32>("keels2_sample_int", 42,
        "Sample bounded integer", FCVAR_NOTIFY, 0, 100, &SamplePlugin::ConVarChanged);
    if (!integer)
    {
        return false;
    }
    limitTeams = FindConVar<int32>("mp_limitteams", &SamplePlugin::ConVarChanged);
    if (!limitTeams)
    {
        LogError("mp_limitteams: {}", LastError());
        return false;
    }
    if (!CreateCommand("keel_sample", "Shows players and ConVars; optional bump", &SamplePlugin::Command) ||
        !ListenForGameEvent("round_start", &SamplePlugin::OnRoundStart))
    {
        return false;
    }
    auto* cvars = GetCVarSystem<ICvar>();
    if (!cvars || !HookPre(cvars, &ICvar::DispatchConCommand, &SamplePlugin::OnCommand))
    {
        LogError("DispatchConCommand hook: {}", LastError());
        return false;
    }
    LogMessage("ready command=keel_sample event=round_start");
    return true;
}

void SamplePlugin::Command(const CCommandContext& context, const CCommand& command)
{
    if (command.ArgC() > 2 || (command.ArgC() == 2 && V_strcmp(command[1], "bump") != 0))
    {
        LogMessage("usage: keel_sample [bump]");
        return;
    }
    if (command.ArgC() == 2)
    {
        int32 next = integer.Get() + 1;
        if (next > integer.Max())
        {
            next = integer.Min();
        }
        if (!integer.Set(next))
        {
            LogError("{}: {}", integer.GetName(), integer.LastError());
            return;
        }
    }
    LogMessage("caller={} int={} mp_limitteams={} players={}",
        context.GetPlayerSlot(), integer.Get(), limitTeams.Get(), CountPlayers());
    if (!context.GetPlayerSlot().IsValid())
    {
        return;
    }
    PlayerInfo player;
    if (!GetPlayer(context.GetPlayerSlot(), player))
    {
        if (LastResult() != KEEL_RESULT_NOT_FOUND)
        {
            LogWarning("player lookup: {}", LastError());
        }
        return;
    }
    DescribePlayer(player.Connection());
}

void SamplePlugin::DescribePlayer(const PlayerConnection& connection)
{
    PlayerInfo player;
    if (!GetPlayer(connection, player))
    {
        return;
    }
    if (!SendToConsole(player.slot, "#{} {} | team={} authenticated={}\n",
            player.user_id, player.name, player.team, player.authenticated) ||
        !SendToChat(player.slot, "Hello {}, player details are in your console.", player.name))
    {
        LogWarning("player output: {}", LastError());
    }
}

int SamplePlugin::CountPlayers()
{
    int count{};
    PlayerInfo player;
    CPlayerSlot after(-1);
    while (GetNextPlayer(after, player))
    {
        after = player.slot;
        if (player.connected && !player.source_tv)
        {
            ++count;
        }
    }
    if (LastResult() != KEEL_RESULT_NOT_FOUND)
    {
        LogWarning("player iteration: {}", LastError());
    }
    return count;
}

void SamplePlugin::ConVarChanged(ConVar<int32>& convar, CSplitScreenSlot slot,
    int32 newValue, int32 oldValue)
{
    LogMessage("{} changed slot={} old={} new={}", convar.GetName(), slot, oldValue, newValue);
}

void SamplePlugin::OnRoundStart(IGameEvent*)
{
    if (!SendToChatAll("A new round has started."))
    {
        LogWarning("round chat: {}", LastError());
    }
}

Action SamplePlugin::OnCommand(ConCommandRef, const CCommandContext&, const CCommand& command)
{
    if (command.ArgC() > 0 && V_strcmp(command[0], "keel_sample") == 0)
    {
        LogMessage("keel_sample observed by the native command hook");
    }
    return PLUGIN_CONTINUE;
}
}

KEELS2_PLUGIN(sample::SamplePlugin)
