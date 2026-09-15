#include <keels2/keels2.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#ifndef KEELS2_CALLBACK_PLUGIN_NAME
#error KEELS2_CALLBACK_PLUGIN_NAME is required
#endif

#ifndef KEELS2_CALLBACK_PLUGIN_MARKER
#error KEELS2_CALLBACK_PLUGIN_MARKER is required
#endif

#ifndef KEELS2_CALLBACK_PLUGIN_PRIORITY
#error KEELS2_CALLBACK_PLUGIN_PRIORITY is required
#endif

#ifndef KEELS2_CALLBACK_PLUGIN_REJECTION
#error KEELS2_CALLBACK_PLUGIN_REJECTION is required
#endif

#ifndef KEELS2_CALLBACK_PLUGIN_COMMAND_ACCEPT
#error KEELS2_CALLBACK_PLUGIN_COMMAND_ACCEPT is required
#endif

namespace
{

std::atomic<bool> g_block_armed{};
std::atomic<bool> g_block_entered{};
std::atomic<bool> g_block_released{};
std::atomic<std::uint32_t> g_unload_count{};

std::string Marker(const char* event)
{
    return std::string("[") + KEELS2_CALLBACK_PLUGIN_MARKER + "] " + event;
}

class Source2CallbacksPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        KEELS2_CALLBACK_PLUGIN_NAME,
        "KeelS2",
        "0.5E",
        "Exercises Source 2 map, event, and decision callbacks"
    };

    bool Load() override
    {
        const bool listening = ListenForGameEvent(
            "round_start",
            &Source2CallbacksPlugin::OnRoundStart);
        LogMessage(Marker(listening ? "loaded" : "listener registration failed").c_str());
        return listening;
    }

    void Unload() override
    {
        g_unload_count.fetch_add(1, std::memory_order_acq_rel);
        LogMessage(Marker("unloaded").c_str());
    }

    std::int32_t CallbackPriority() const noexcept override
    {
        return KEELS2_CALLBACK_PLUGIN_PRIORITY;
    }

    void OnLevelInit(
        KeyValues* key_values,
        ILoopModePrerequisiteRegistry* prerequisite_registry) override
    {
        LogMessage(Marker(
            key_values && prerequisite_registry ? "LevelInit" : "LevelInit invalid").c_str());
        if (g_block_armed.exchange(false, std::memory_order_acq_rel))
        {
            g_block_entered.store(true, std::memory_order_release);
            while (!g_block_released.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        }
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
        const bool valid = slot.Get() == 4 && name && std::strcmp(name, "Keel") == 0 &&
            xuid == 76561198000000004ull && network_id &&
            std::strcmp(network_id, "STEAM_1:0:2") == 0 && !unknown && rejection_message;
        LogMessage(Marker(valid ? "ClientConnect" : "ClientConnect invalid").c_str());
        if (!valid)
        {
            return true;
        }
#if KEELS2_CALLBACK_PLUGIN_REJECTION == 1
        std::array<char, KEELS2_SOURCE2_REJECTION_CAPACITY> rejection{};
        rejection.fill('A');
        rejection_message->Insert(
            0,
            rejection.data(),
            static_cast<int>(rejection.size() - 1));
        return false;
#elif KEELS2_CALLBACK_PLUGIN_REJECTION == 2
        rejection_message->Insert(0, "second tie rejection");
        return false;
#elif KEELS2_CALLBACK_PLUGIN_REJECTION == 3
        rejection_message->Insert(0, "peer rejection wins");
        return false;
#else
        return true;
#endif
    }

    bool OnClientCommand(CPlayerSlot slot, const CCommand& command) override
    {
        const bool valid = slot.Get() == 4 && command.ArgC() == 2 &&
            std::strcmp(command[0], "say") == 0 &&
            std::strcmp(command[1], "keels2") == 0;
        LogMessage(Marker(valid ? "ClientCommand" : "ClientCommand invalid").c_str());
        return valid && KEELS2_CALLBACK_PLUGIN_COMMAND_ACCEPT != 0;
    }

private:
    void OnRoundStart(IGameEvent* event)
    {
        const bool valid = event && std::strcmp(event->GetName(), "round_start") == 0;
        LogMessage(Marker(valid ? "round_start" : "round_start invalid").c_str());
    }
};

}

KEELS2_PLUGIN(Source2CallbacksPlugin)

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_Source2ArmBlock()
{
    g_block_released.store(false, std::memory_order_release);
    g_block_entered.store(false, std::memory_order_release);
    g_block_armed.store(true, std::memory_order_release);
}

extern "C" KEELS2_PLUGIN_EXPORT bool KeelTest_Source2BlockEntered()
{
    return g_block_entered.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_Source2ReleaseBlock()
{
    g_block_released.store(true, std::memory_order_release);
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_Source2UnloadCount()
{
    return g_unload_count.load(std::memory_order_acquire);
}
