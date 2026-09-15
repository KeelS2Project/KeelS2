#include <keels2/authoring.hpp>
#include <chrono>
#include <optional>

namespace docs
{
using namespace keels2::authoring;

class Reminder final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Reminder", "KeelS2 documentation", "1.0.0",
        "A delayed message checked on the game thread"};

    bool Load() override
    {
        return CreateCommand("keel_docs_remind", "Remind the latest caller after three seconds", &Reminder::Schedule,
            FCVAR_GAMEDLL | FCVAR_CLIENT_CAN_EXECUTE);
    }

    void OnGameFrame(bool, bool, bool) override
    {
        if (!pending || Clock::now() < due) return;
        const auto connection = *pending;
        pending.reset();
        PlayerInfo player;
        if (GetPlayer(connection, player) && !SendToChat(player.slot, "Your three-second reminder."))
            LogWarning("Reminder delivery: {}", LastError());
    }

    void OnLevelShutdown() override { pending.reset(); }
    bool PreparePause() override { pending.reset(); return true; }

private:
    using Clock = std::chrono::steady_clock;
    void Schedule(const CCommandContext& context, const CCommand& command)
    {
        if (command.ArgC() != 1) { LogMessage("Usage: keel_docs_remind"); return; }
        PlayerInfo player;
        if (!GetPlayer(context.GetPlayerSlot(), player))
        {
            LogMessage("Run keel_docs_remind from a connected client's console.");
            return;
        }
        pending = player.Connection();
        due = Clock::now() + std::chrono::seconds(3);
        if (!SendToConsole(player.slot, "Reminder scheduled; it replaces any previous request.\n"))
            LogWarning("Reminder confirmation: {}", LastError());
    }

    std::optional<PlayerConnection> pending;
    Clock::time_point due;
};
}

KEELS2_PLUGIN(docs::Reminder)
