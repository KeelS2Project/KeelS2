#include "plugin.h"

#include <string>

bool CallbacksPlugin::Load()
{
    if (!ListenForGameEvent("round_start", &CallbacksPlugin::OnRoundStart))
    {
        LogError("could not listen for round_start");
        return false;
    }
    LogMessage("ready for Source 2 callbacks");
    return true;
}

void CallbacksPlugin::Unload()
{
    LogMessage("unloaded after Source 2 callback cleanup");
}

void CallbacksPlugin::OnLevelInit(
    KeyValues* key_values,
    ILoopModePrerequisiteRegistry* prerequisite_registry)
{
    LogMessage(
        key_values && prerequisite_registry
            ? "level initialized"
            : "level initialized without optional context");
}

void CallbacksPlugin::OnLevelShutdown()
{
    LogMessage("level shutdown");
}

bool CallbacksPlugin::OnClientConnect(
    CPlayerSlot slot,
    const char* name,
    uint64 xuid,
    const char* network_id,
    bool unknown,
    CBufferString* rejection_message)
{
    static_cast<void>(unknown);
    static_cast<void>(rejection_message);
    const std::string message =
        "client-connect decision: slot=" + std::to_string(slot.Get()) +
        " name=" + (name ? name : "") +
        " xuid=" + std::to_string(xuid) +
        " network_id=" + (network_id ? network_id : "") +
        " accept=1";
    LogMessage(message.c_str());
    return true;
}

bool CallbacksPlugin::OnClientCommand(CPlayerSlot slot, const CCommand& command)
{
    const std::string message =
        "client-command decision: slot=" + std::to_string(slot.Get()) +
        " command=" + (command.ArgC() > 0 ? command[0] : "") +
        " accept=1";
    LogMessage(message.c_str());
    return true;
}

void CallbacksPlugin::OnRoundStart(IGameEvent* event)
{
    if (event)
    {
        LogMessage("round_start game event");
    }
}

KEELS2_PLUGIN(CallbacksPlugin)
