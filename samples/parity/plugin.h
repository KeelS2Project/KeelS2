#ifndef KEELS2_SAMPLE_PARITY_PLUGIN_H
#define KEELS2_SAMPLE_PARITY_PLUGIN_H

#include <keels2/keels2.hpp>

#include <atomic>
#include <cstdint>

class ParityPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 Source 2 Parity Sample",
        "KeelS2 Project",
        "0.5.0",
        "Practical native behavior from the official Metamod:Source Source 2 sample"
    };

    bool Load() override;
    void Unload() override;
    void OnLevelInit(
        KeyValues* key_values,
        ILoopModePrerequisiteRegistry* prerequisite_registry) override;
    void OnLevelShutdown() override;
    bool OnClientConnect(
        CPlayerSlot slot,
        const char* name,
        uint64 xuid,
        const char* network_id,
        bool unknown,
        CBufferString* rejection_message) override;
    bool OnClientCommand(CPlayerSlot slot, const CCommand& command) override;
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
    void OnAllPluginsLoaded() override;

private:
    void Command(const CCommandContext& context, const CCommand& command);
    void IntegerChanged(
        CConVarRef<std::int32_t>* convar,
        CSplitScreenSlot slot,
        const std::int32_t* new_value,
        const std::int32_t* old_value);
    void OnRoundStart(IGameEvent* event);
    void LogClient(const char* phase, CPlayerSlot slot, const char* name, uint64 xuid);

    CConVarRef<std::int32_t>* integer_{};
    CConVarRef<float>* floating_{};
    CConVarRef<std::int32_t>* limit_teams_{};
    std::atomic<bool> game_frame_logged_{};
};

#endif
