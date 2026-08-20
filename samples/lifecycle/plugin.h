#ifndef KEELS2_SAMPLE_LIFECYCLE_PLUGIN_H
#define KEELS2_SAMPLE_LIFECYCLE_PLUGIN_H

#include <keels2/keels2.hpp>

#include <atomic>

class LifecyclePlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 Lifecycle Example",
        "KeelS2 Project",
        "0.1.0-dev",
        "Observes Source 2 frame and client lifecycle events"
    };

    bool Load() override;
    void OnGameFrame(bool simulating, bool first_tick, bool last_tick) override;
    void OnClientConnected(
        CPlayerSlot slot,
        const char* name,
        uint64 xuid,
        const char* network_id,
        const char* address,
        bool fake_player) override;
    void OnClientPutInServer(
        CPlayerSlot slot,
        const char* name,
        int client_type,
        uint64 xuid) override;
    void OnClientActive(
        CPlayerSlot slot,
        bool load_game,
        const char* name,
        uint64 xuid) override;
    void OnClientFullyConnected(CPlayerSlot slot) override;
    void OnClientDisconnecting(
        CPlayerSlot slot,
        ENetworkDisconnectionReason reason,
        const char* name,
        uint64 xuid,
        const char* network_id) override;
    void OnClientSettingsChanged(CPlayerSlot slot) override;

private:
    std::atomic<bool> game_frame_logged_{};
};

#endif
