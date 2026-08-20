#include <keels2/keels2.hpp>

#include <atomic>
#include <cstring>
#include <string>

#ifndef KEELS2_LIVE_CALLBACK_PLUGIN_NAME
#error KEELS2_LIVE_CALLBACK_PLUGIN_NAME is required
#endif

#ifndef KEELS2_LIVE_CALLBACK_MARKER
#error KEELS2_LIVE_CALLBACK_MARKER is required
#endif

#ifndef KEELS2_LIVE_CALLBACK_PRIORITY
#error KEELS2_LIVE_CALLBACK_PRIORITY is required
#endif

#ifndef KEELS2_LIVE_CALLBACK_REJECTION
#error KEELS2_LIVE_CALLBACK_REJECTION is required
#endif

namespace
{

std::atomic<bool> g_reject_next_real{KEELS2_LIVE_CALLBACK_REJECTION != 0};

std::string Marker(const std::string& message)
{
    return std::string("[") + KEELS2_LIVE_CALLBACK_MARKER + "] " + message;
}

class LiveCallbacksPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        KEELS2_LIVE_CALLBACK_PLUGIN_NAME,
        "KeelS2",
        "0.5E-acceptance",
        "Exact-candidate Source 2 callback acceptance helper"
    };

    bool Load() override
    {
        const bool listening = ListenForGameEvent(
            "round_start",
            &LiveCallbacksPlugin::OnRoundStart);
        LogMessage(Marker(listening ? "loaded" : "listener registration failed").c_str());
        return listening;
    }

    void Unload() override
    {
        LogMessage(Marker("unloaded").c_str());
    }

    std::int32_t CallbackPriority() const noexcept override
    {
        return KEELS2_LIVE_CALLBACK_PRIORITY;
    }

    void OnLevelInit(
        KeyValues* key_values,
        ILoopModePrerequisiteRegistry* prerequisite_registry) override
    {
        const std::string state = key_values && prerequisite_registry
            ? "LevelInit context=complete"
            : "LevelInit context=partial";
        LogMessage(Marker(state).c_str());
    }

    void OnLevelShutdown() override
    {
        LogMessage(Marker("LevelShutdown").c_str());
    }

    bool OnClientConnect(
        CPlayerSlot slot,
        const char* name,
        uint64 xuid,
        const char* network_id,
        bool unknown,
        CBufferString* rejection_message) override
    {
        const bool real = xuid != 0 && network_id && std::strcmp(network_id, "BOT") != 0;
        const bool reject = real && KEELS2_LIVE_CALLBACK_REJECTION != 0 &&
            g_reject_next_real.exchange(false, std::memory_order_acq_rel);
        std::string message =
            "ClientConnect priority=" + std::to_string(CallbackPriority()) +
            " slot=" + std::to_string(slot.Get()) +
            " name=" + (name ? name : "") +
            " xuid=" + std::to_string(xuid) +
            " network_id=" + (network_id ? network_id : "") +
            " unknown=" + (unknown ? "1" : "0") +
            " decision=" + (reject ? "reject" : "accept");
        LogMessage(Marker(message).c_str());
        if (reject && rejection_message)
        {
#if KEELS2_LIVE_CALLBACK_REJECTION == 1
            rejection_message->Insert(0, "KeelS2 0.5E priority rejection");
#else
            rejection_message->Insert(0, "KeelS2 0.5E later tie rejection");
#endif
        }
        return !reject;
    }

    bool OnClientCommand(CPlayerSlot slot, const CCommand& command) override
    {
        const char* verb = command.ArgC() > 0 ? command[0] : "";
        const char* argument = command.ArgC() > 1 ? command[1] : "";
        const bool blocked_probe = std::strcmp(verb, "keels2_blocked") == 0 ||
            (std::strcmp(verb, "say") == 0 &&
                std::strcmp(argument, "keels2_blocked") == 0);
        const bool block = KEELS2_LIVE_CALLBACK_REJECTION == 1 &&
            blocked_probe;
        const std::string message =
            "ClientCommand priority=" + std::to_string(CallbackPriority()) +
            " verb=" + verb +
            " argument=" + argument +
            " slot=" + std::to_string(slot.Get()) +
            " decision=" + (block ? "reject" : "accept");
        LogMessage(Marker(message).c_str());
        return !block;
    }

private:
    void OnRoundStart(IGameEvent* event)
    {
        LogMessage(Marker(event ? "event=round_start" : "event=round_start invalid").c_str());
    }
};

}

KEELS2_PLUGIN(LiveCallbacksPlugin)
