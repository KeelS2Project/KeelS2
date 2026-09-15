#include <keels2/authoring.hpp>

namespace docs
{
using namespace keels2::authoring;
class Events final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Events", "KeelS2 documentation", "1.0.0", "Game events and lifecycle callbacks"};
    bool Load() override { return ListenForGameEvent("round_start", &Events::RoundStart) &&
        CreateCommand("keel_docs_stop_events", "Stop round announcements", &Events::StopEvents); }
    int32 CallbackPriority() const noexcept override { return 10; }
    void OnAllPluginsLoaded() override { LogMessage("Initial plugin loading completed."); }
    bool OnClientConnect(CPlayerSlot slot, const char*, uint64, const char*, bool, CBufferString*) override
    { LogMessage("Connection proposed for slot {}", slot); return true; }
    bool OnClientCommand(CPlayerSlot, const CCommand&) override { return true; }
    void OnClientConnected(CPlayerSlot slot, const char*, uint64, const char*, const char*, bool) override
    { LogMessage("Transport connected for slot {}", slot); }
    void OnClientPutInServer(CPlayerSlot slot, const char*, int, uint64) override
    { LogMessage("Put in server: {}", slot); }
    void OnClientActive(CPlayerSlot slot, bool, const char*, uint64) override
    { LogMessage("Active slot {}", slot); }
    void OnClientSettingsChanged(CPlayerSlot slot) override
    { LogMessage("Settings changed for slot {}", slot); }
    void OnClientFullyConnected(CPlayerSlot slot) override { LogMessage("Connected slot {}", slot); }
    void OnClientDisconnecting(CPlayerSlot slot, ENetworkDisconnectionReason, const char*, uint64, const char*) override
    { LogMessage("Disconnecting slot {}", slot); }
    void OnLevelInit(KeyValues*, ILoopModePrerequisiteRegistry*) override { frames = 0; }
    void OnGameFrame(bool simulating, bool, bool) override { if (simulating) ++frames; }
    void OnLevelShutdown() override { LogMessage("Simulated {} frames this map", frames); }
    void Unload() override { LogMessage("Plugin-owned state can now be released."); }
private:
    void StopEvents(const CCommandContext&, const CCommand&)
    {
        if (!StopListeningForGameEvent("round_start")) LogWarning("No active round listener was removed.");
    }
    void RoundStart(IGameEvent* event)
    {
        if (event && !SendToChatAll("A new round has started."))
            LogWarning("Round message: {}", LastError());
    }
    uint64 frames{};
};
}
KEELS2_PLUGIN(docs::Events)
