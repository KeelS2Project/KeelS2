#ifndef KEELS2_SAMPLE_PLUGIN_H
#define KEELS2_SAMPLE_PLUGIN_H

#include <keels2/keels2.hpp>

class SamplePlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 Source 2 Sample",
        "KeelS2 Project",
        "0.9.0",
        "Source 2 lifecycle, commands, events, and ConVars"
    };

    bool Load() override;
    void Unload() override;

    void OnLevelInit(
        KeyValues* keyValues,
        ILoopModePrerequisiteRegistry* prerequisiteRegistry) override;

    void OnLevelShutdown() override;

    bool OnClientConnect(
        CPlayerSlot slot,
        const char* name,
        uint64 xuid,
        const char* networkId,
        bool unknown,
        CBufferString* rejectionMessage) override;

    bool OnClientCommand(
        CPlayerSlot slot,
        const CCommand& command) override;

    void OnGameFrame(
        bool simulating,
        bool firstTick,
        bool lastTick) override;

    void OnClientConnected(
        CPlayerSlot slot,
        const char* name,
        uint64 xuid,
        const char* networkId,
        const char* address,
        bool fakePlayer) override;

    void OnClientPutInServer(
        CPlayerSlot slot,
        const char* name,
        int clientType,
        uint64 xuid) override;

    void OnClientActive(
        CPlayerSlot slot,
        bool loadGame,
        const char* name,
        uint64 xuid) override;

    void OnClientFullyConnected(CPlayerSlot slot) override;

    void OnClientDisconnecting(
        CPlayerSlot slot,
        ENetworkDisconnectionReason reason,
        const char* name,
        uint64 xuid,
        const char* networkId) override;

    void OnClientSettingsChanged(CPlayerSlot slot) override;
    void OnAllPluginsLoaded() override;

private:
    void Command(
        const CCommandContext& context,
        const CCommand& command);

    void IntegerChanged(
        ConVar<int>& convar,
        CSplitScreenSlot slot,
        int newValue,
        int oldValue);

    void OnRoundStart(IGameEvent* event);

    bool gameFrameLogged = false;
    ConVar<int> integer;
    ConVar<float> floating;
    ConVar<int> limitTeams;
};

#endif
