#include <keels2/authoring.hpp>

namespace docs
{
using namespace keels2::authoring;
class Players final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Players", "KeelS2 documentation", "1.0.0", "Player lookup, iteration and connection identity"};
    bool Load() override
    {
        return CreateCommand("keel_docs_players", "List players and remember the caller", &Players::List, FCVAR_GAMEDLL | FCVAR_CLIENT_CAN_EXECUTE)
            && CreateCommand("keel_docs_remembered", "Look up the remembered connection", &Players::Remembered, FCVAR_GAMEDLL | FCVAR_CLIENT_CAN_EXECUTE);
    }
private:
    void List(const CCommandContext& context, const CCommand&)
    {
        const auto service = PlayerServiceStatus();
        if (service != KEEL_RESULT_OK)
        {
            LogWarning("Player service is unavailable (result {}).", static_cast<int>(service));
            return;
        }
        PlayerInfo player;
        CPlayerSlot after(-1);
        while (GetNextPlayer(after, player))
        {
            after = player.slot;
            LogMessage("slot={} name={} user={} connected={} bot={} team={} alive={}", player.slot,
                player.name, player.user_id, player.connected, player.bot, player.team, player.alive);
        }
        if (LastResult() != KEEL_RESULT_NOT_FOUND)
            LogError("Iteration failed: {}", LastError());
        if (GetPlayer(context.GetPlayerSlot(), player))
        {
            remembered = player.Connection();
            hasRemembered = true;
            if (!SendToConsole(player.slot, "Your connection has been remembered.\n"))
                LogWarning("Console output: {}", LastError());
        }
    }
    void Remembered(const CCommandContext&, const CCommand&)
    {
        PlayerInfo current;
        if (!hasRemembered || !GetPlayer(remembered, current))
        {
            LogMessage("That connection is no longer present.");
            return;
        }
        PlayerInfo byUserId;
        if (GetPlayerByUserId(current.user_id, byUserId))
            LogMessage("Current player is {}", byUserId.name);
        if (current.authenticated && !current.source_tv)
            if (!SendToChat(current.slot, "Hello again, {}.", current.name))
                LogWarning("Chat output: {}", LastError());
    }
    PlayerConnection remembered{};
    bool hasRemembered{};
};
}
KEELS2_PLUGIN(docs::Players)
