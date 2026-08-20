#include "plugin.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{

const char* Text(const char* value)
{
    return value ? value : "";
}

}

bool ParityPlugin::Load()
{
    game_frame_logged_.store(false, std::memory_order_release);
    integer_ = CreateConVar<std::int32_t>(
        "keels2_parity_int",
        FCVAR_NOTIFY,
        "KeelS2 parity sample bounded integer",
        std::int32_t{42},
        true,
        std::int32_t{0},
        true,
        std::int32_t{100},
        &ParityPlugin::IntegerChanged);
    floating_ = CreateConVar<float>(
        "keels2_parity_float",
        FCVAR_NONE,
        "KeelS2 parity sample bounded float",
        1.25F,
        true,
        0.25F,
        true,
        4.0F);
    limit_teams_ = FindConVar<std::int32_t>("mp_limitteams");
    const bool command = CreateCommand(
        "keel_parity",
        "Reports or advances the KeelS2 Source 2 parity sample",
        &ParityPlugin::Command);
    const bool event = ListenForGameEvent("round_start", &ParityPlugin::OnRoundStart);
    if (!integer_ || !floating_ || !limit_teams_ || !command || !event)
    {
        LogError("[Parity Sample] registration failed");
        return false;
    }
    LogMessage("[Parity Sample] ready command=keel_parity event=round_start convar=mp_limitteams");
    return true;
}

void ParityPlugin::Unload()
{
    LogMessage("[Parity Sample] unloaded; ordinary resources required no manual cleanup");
}

void ParityPlugin::OnLevelInit(
    KeyValues* key_values,
    ILoopModePrerequisiteRegistry* prerequisite_registry)
{
    LogMessage(
        key_values && prerequisite_registry
            ? "[Parity Sample] LevelInit context=complete"
            : "[Parity Sample] LevelInit context=partial");
}

void ParityPlugin::OnLevelShutdown()
{
    LogMessage("[Parity Sample] LevelShutdown");
}

bool ParityPlugin::OnClientConnect(
    CPlayerSlot slot,
    const char* name,
    uint64 xuid,
    const char* network_id,
    bool unknown,
    CBufferString* rejection_message)
{
    static_cast<void>(rejection_message);
    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "[Parity Sample] ClientConnect slot=%d name=%s xuid=%llu network_id=%s unknown=%u decision=accept",
        slot.Get(),
        Text(name),
        static_cast<unsigned long long>(xuid),
        Text(network_id),
        unknown ? 1u : 0u);
    LogMessage(message);
    return true;
}

bool ParityPlugin::OnClientCommand(CPlayerSlot slot, const CCommand& command)
{
    const char* verb = command.ArgC() > 0 ? command[0] : "";
    const char* argument = command.ArgC() > 1 ? command[1] : "";
    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "[Parity Sample] ClientCommand slot=%d verb=%s argument=%s decision=accept",
        slot.Get(),
        verb,
        argument);
    LogMessage(message);
    return true;
}

void ParityPlugin::OnGameFrame(bool simulating, bool first_tick, bool last_tick)
{
    if (game_frame_logged_.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }
    char message[192]{};
    std::snprintf(
        message,
        sizeof(message),
        "[Parity Sample] GameFrame simulating=%u first_tick=%u last_tick=%u",
        simulating ? 1u : 0u,
        first_tick ? 1u : 0u,
        last_tick ? 1u : 0u);
    LogMessage(message);
}

void ParityPlugin::OnClientConnected(
    CPlayerSlot slot,
    const char* name,
    uint64 xuid,
    const char* network_id,
    const char* address,
    bool fake_player)
{
    char message[640]{};
    std::snprintf(
        message,
        sizeof(message),
        "[Parity Sample] ClientConnected slot=%d name=%s xuid=%llu network_id=%s address=%s fake=%u",
        slot.Get(),
        Text(name),
        static_cast<unsigned long long>(xuid),
        Text(network_id),
        Text(address),
        fake_player ? 1u : 0u);
    LogMessage(message);
}

void ParityPlugin::OnClientPutInServer(
    CPlayerSlot slot,
    const char* name,
    int client_type,
    uint64 xuid)
{
    char phase[64]{};
    std::snprintf(phase, sizeof(phase), "ClientPutInServer client_type=%d", client_type);
    LogClient(phase, slot, name, xuid);
}

void ParityPlugin::OnClientActive(
    CPlayerSlot slot,
    bool load_game,
    const char* name,
    uint64 xuid)
{
    char phase[64]{};
    std::snprintf(phase, sizeof(phase), "ClientActive load_game=%u", load_game ? 1u : 0u);
    LogClient(phase, slot, name, xuid);
}

void ParityPlugin::OnClientFullyConnected(CPlayerSlot slot)
{
    char message[128]{};
    std::snprintf(message, sizeof(message), "[Parity Sample] ClientFullyConnected slot=%d", slot.Get());
    LogMessage(message);
}

void ParityPlugin::OnClientDisconnecting(
    CPlayerSlot slot,
    ENetworkDisconnectionReason reason,
    const char* name,
    uint64 xuid,
    const char* network_id)
{
    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "[Parity Sample] ClientDisconnecting slot=%d name=%s xuid=%llu network_id=%s reason=%d",
        slot.Get(),
        Text(name),
        static_cast<unsigned long long>(xuid),
        Text(network_id),
        static_cast<int>(reason));
    LogMessage(message);
}

void ParityPlugin::OnClientSettingsChanged(CPlayerSlot slot)
{
    char message[128]{};
    std::snprintf(message, sizeof(message), "[Parity Sample] ClientSettingsChanged slot=%d", slot.Get());
    LogMessage(message);
}

void ParityPlugin::OnAllPluginsLoaded()
{
    LogMessage("[Parity Sample] AllPluginsLoaded");
}

void ParityPlugin::Command(const CCommandContext& context, const CCommand& command)
{
    static_cast<void>(context);
    if (command.ArgC() > 2 ||
        (command.ArgC() == 2 && std::strcmp(command[1], "bump") != 0))
    {
        LogError("[Parity Sample] usage: keel_parity [bump]");
        return;
    }
    if (command.ArgC() == 2)
    {
        integer_->Set(integer_->Get() == 100 ? 0 : integer_->Get() + 1);
        floating_->Set((std::min)(4.0F, floating_->Get() + 0.25F));
    }
    const std::string message =
        "[Parity Sample] values int=" + std::to_string(integer_->Get()) +
        " float=" + std::to_string(floating_->Get()) +
        " mp_limitteams=" + std::to_string(limit_teams_->Get());
    LogMessage(message.c_str());
}

void ParityPlugin::IntegerChanged(
    CConVarRef<std::int32_t>* convar,
    CSplitScreenSlot slot,
    const std::int32_t* new_value,
    const std::int32_t* old_value)
{
    if (!convar || !new_value || !old_value)
    {
        return;
    }
    const std::string message =
        "[Parity Sample] " + std::string(convar->GetName()) +
        " changed slot=" + std::to_string(slot.Get()) +
        " old=" + std::to_string(*old_value) +
        " new=" + std::to_string(*new_value);
    LogMessage(message.c_str());
}

void ParityPlugin::OnRoundStart(IGameEvent* event)
{
    LogMessage(
        event && std::strcmp(event->GetName(), "round_start") == 0
            ? "[Parity Sample] event=round_start"
            : "[Parity Sample] event=round_start invalid");
}

void ParityPlugin::LogClient(
    const char* phase,
    CPlayerSlot slot,
    const char* name,
    uint64 xuid)
{
    char message[384]{};
    std::snprintf(
        message,
        sizeof(message),
        "[Parity Sample] %s slot=%d name=%s xuid=%llu",
        phase,
        slot.Get(),
        Text(name),
        static_cast<unsigned long long>(xuid));
    LogMessage(message);
}

KEELS2_PLUGIN(ParityPlugin)
