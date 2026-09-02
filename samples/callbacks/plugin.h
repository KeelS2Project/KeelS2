#ifndef KEELS2_SAMPLE_CALLBACKS_PLUGIN_H
#define KEELS2_SAMPLE_CALLBACKS_PLUGIN_H

#include <keels2/keels2.hpp>

class CallbacksPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 Callbacks Sample",
        "KeelS2 Project",
        "0.9.0",
        "Source 2 level, game-event, and decision callbacks"
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

private:
    void OnRoundStart(IGameEvent* event);
};

#endif
