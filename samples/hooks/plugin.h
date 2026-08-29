#ifndef KEELS2_SAMPLE_HOOKS_PLUGIN_H
#define KEELS2_SAMPLE_HOOKS_PLUGIN_H

#include <keels2/keels2.hpp>

class HooksPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 Hooks Example",
        "KeelS2 Project",
        "0.8.0",
        "Typed hooks with Valve method signatures"
    };

    bool Load() override;

private:
    PluginResult GameFramePre(
        bool simulating,
        bool firstTick,
        bool lastTick);

    PluginResult GameFramePost(
        bool simulating,
        bool firstTick,
        bool lastTick);

    PluginResult ClientConnectPost(
        CPlayerSlot slot,
        const char* name,
        uint64 xuid,
        const char* networkId,
        bool unknown,
        CBufferString* rejectionMessage);

    PluginResult ClientCommandPre(CPlayerSlot slot, const CCommand& command);

    PluginResult ClientDisconnectPre(
        CPlayerSlot slot,
        ENetworkDisconnectionReason reason,
        const char* name,
        uint64 xuid,
        const char* networkId);

    bool gameFramePreLogged{};
    bool gameFramePostLogged{};
    bool clientConnectLogged{};
    bool clientCommandLogged{};
    bool clientDisconnectLogged{};
};

#endif
