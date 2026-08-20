#include "plugin.h"

#include <cstdio>

namespace
{

const char* Text(const char* value)
{
    return value ? value : "";
}

}

bool LifecyclePlugin::Load()
{
    game_frame_logged_.store(false, std::memory_order_release);
    LogMessage("[Lifecycle Example] ready for Source 2 lifecycle callbacks");
    return true;
}

void LifecyclePlugin::OnGameFrame(bool simulating, bool first_tick, bool last_tick)
{
    if (game_frame_logged_.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }
    char message[512]{};
    std::snprintf(message, sizeof(message),
        "[Lifecycle Example] GameFrame simulating=%u first_tick=%u last_tick=%u",
        simulating ? 1u : 0u, first_tick ? 1u : 0u, last_tick ? 1u : 0u);
    LogMessage(message);
}

void LifecyclePlugin::OnClientConnected(
    CPlayerSlot slot,
    const char* name,
    uint64 xuid,
    const char* network_id,
    const char* address,
    bool fake_player)
{
    char message[512]{};
    std::snprintf(message, sizeof(message),
        "[Lifecycle Example] ClientConnected slot=%d xuid=%llu name=%s network_id=%s address=%s fake=%u",
        slot.Get(), static_cast<unsigned long long>(xuid), Text(name), Text(network_id), Text(address),
        fake_player ? 1u : 0u);
    LogMessage(message);
}

void LifecyclePlugin::OnClientPutInServer(
    CPlayerSlot slot,
    const char* name,
    int client_type,
    uint64 xuid)
{
    char message[512]{};
    std::snprintf(message, sizeof(message),
        "[Lifecycle Example] ClientPutInServer slot=%d xuid=%llu name=%s client_type=%d",
        slot.Get(), static_cast<unsigned long long>(xuid), Text(name), client_type);
    LogMessage(message);
}

void LifecyclePlugin::OnClientActive(
    CPlayerSlot slot,
    bool load_game,
    const char* name,
    uint64 xuid)
{
    char message[512]{};
    std::snprintf(message, sizeof(message),
        "[Lifecycle Example] ClientActive slot=%d xuid=%llu name=%s load_game=%u",
        slot.Get(), static_cast<unsigned long long>(xuid), Text(name), load_game ? 1u : 0u);
    LogMessage(message);
}

void LifecyclePlugin::OnClientFullyConnected(CPlayerSlot slot)
{
    char message[128]{};
    std::snprintf(message, sizeof(message),
        "[Lifecycle Example] ClientFullyConnected slot=%d", slot.Get());
    LogMessage(message);
}

void LifecyclePlugin::OnClientDisconnecting(
    CPlayerSlot slot,
    ENetworkDisconnectionReason reason,
    const char* name,
    uint64 xuid,
    const char* network_id)
{
    char message[512]{};
    std::snprintf(message, sizeof(message),
        "[Lifecycle Example] ClientDisconnecting slot=%d xuid=%llu name=%s network_id=%s reason=%d",
        slot.Get(), static_cast<unsigned long long>(xuid), Text(name), Text(network_id),
        static_cast<int>(reason));
    LogMessage(message);
}

void LifecyclePlugin::OnClientSettingsChanged(CPlayerSlot slot)
{
    char message[128]{};
    std::snprintf(message, sizeof(message),
        "[Lifecycle Example] ClientSettingsChanged slot=%d", slot.Get());
    LogMessage(message);
}

KEELS2_PLUGIN(LifecyclePlugin)
