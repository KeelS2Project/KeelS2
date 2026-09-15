#include <keels2/authoring.hpp>
#include <keels2/native_runtime.h>

#include <string_view>

namespace docs
{
class PlayerAbi final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Docs Player ABI", "KeelS2 documentation", "1.0.0",
        "Player snapshots, saved connections, sampled input and literal output"
    };

    bool Load() override
    {
        if (!Connect(KEELS2_PLAYERS_SERVICE_NAME, KEELS2_PLAYERS_API_VERSION, players) ||
            !Connect(KEELS2_PLAYER_INPUT_SERVICE_NAME, KEELS2_PLAYER_INPUT_API_VERSION, input) ||
            !Connect(KEELS2_NATIVE_RUNTIME_SERVICE_NAME, KEELS2_NATIVE_RUNTIME_API_VERSION, runtime) ||
            !players->get_player || !players->get_next_player || !players->validate_connection ||
            !input->read || !runtime->check_game_thread || !runtime->client_console_print ||
            !runtime->client_chat_print || !runtime->broadcast_chat) return false;

        return CreateCommand("keel_docs_player_api", "Inspect player and input service tables",
            &PlayerAbi::Command, FCVAR_GAMEDLL | FCVAR_CLIENT_CAN_EXECUTE);
    }

    bool PreparePause() override { remembered = {}; return true; }
    void OnLevelShutdown() override { remembered = {}; }
    void Unload() override
    {
        remembered = {};
        players = nullptr;
        input = nullptr;
        runtime = nullptr;
    }

private:
    template <typename Api>
    bool Connect(const char* name, uint32_t version, const Api*& api)
    {
        const void* service{};
        if (!Check(HostContext().QueryService(name, version, &service), name)) return false;
        api = static_cast<const Api*>(service);
        return api && api->size == sizeof(Api) && api->api_version == version;
    }

    KeelPluginHandle Owner() const { return HostContext().PluginHandle(); }

    bool Check(KeelResult result, const char* operation)
    {
        if (result != KEEL_RESULT_OK) LogWarning("{}: result {}", operation, result);
        return result == KEEL_RESULT_OK;
    }

    static KeelPlayerInfo EmptyPlayer()
    {
        KeelPlayerInfo player{};
        player.size = sizeof(player);
        return player;
    }

    void List()
    {
        int32_t after = -1;
        for (;;)
        {
            auto player = EmptyPlayer();
            const auto result = players->get_next_player(Owner(), after, &player);
            if (result == KEEL_RESULT_NOT_FOUND) return;
            if (!Check(result, "List player")) return;
            LogMessage("slot={} user={} generation={} name={} flags={} steam={} team={} controller={} pawn={}",
                player.slot, player.user_id, player.connection, player.name, player.flags,
                player.steam_id, player.team, player.controller_handle, player.pawn_handle);
            after = player.slot;
        }
    }

    void Remember(CPlayerSlot slot)
    {
        auto player = EmptyPlayer();
        if (!Check(players->get_player(Owner(), slot.Get(), &player), "Remember player")) return;
        remembered = {player.slot, 0, player.connection};
        LogMessage("Remembered slot={} generation={}.", remembered.slot, remembered.generation);
    }

    bool HasRemembered()
    {
        if (remembered.generation) return true;
        LogMessage("Run keel_docs_player_api remember from a connected client's console first.");
        return false;
    }

    bool Current(KeelPlayerInfo& player)
    {
        if (!HasRemembered()) return false;
        player = EmptyPlayer();
        return Check(players->validate_connection(Owner(), &remembered, &player), "Validate remembered player");
    }

    void Input()
    {
        if (!HasRemembered()) return;
        KeelPlayerInput state{sizeof(state), 0, 0, 0};
        if (!Check(input->read(Owner(), &remembered, &state), "Read input")) return;
        LogMessage("Input slot={} held={} context={} use={}.", remembered.slot, state.buttons, state.context,
            (state.buttons & KEELS2_BUTTON_USE) != 0);
    }

    void Output(bool chat)
    {
        KeelPlayerInfo player{};
        if (!Current(player)) return;
        constexpr const char* text = "100% {literal}; this is text.\n";
        const auto result = chat ? runtime->client_chat_print(Owner(), player.slot, text)
                                 : runtime->client_console_print(Owner(), player.slot, text);
        Check(result, chat ? "Print chat" : "Print console");
    }

    void Command(const CCommandContext& context, const CCommand& command)
    {
        if (command.ArgC() != 2)
        {
            LogMessage("Usage: keel_docs_player_api list|remember|check|input|console|chat|broadcast|thread");
            return;
        }
        const std::string_view action(command.Arg(1));
        if (action == "list") List();
        else if (action == "remember") Remember(context.GetPlayerSlot());
        else if (action == "check")
        {
            KeelPlayerInfo player{};
            if (Current(player)) LogMessage("Current player: {} (user {}).", player.name, player.user_id);
        }
        else if (action == "input") Input();
        else if (action == "console") Output(false);
        else if (action == "chat") Output(true);
        else if (action == "broadcast")
        {
            if (context.GetPlayerSlot().Get() >= 0)
                LogMessage("Run the broadcast example from the server console.");
            else Check(runtime->broadcast_chat(Owner(), "Player API example broadcast."), "Broadcast chat");
        }
        else if (action == "thread")
            LogMessage("Game-thread check: result {}.", runtime->check_game_thread(Owner()));
        else LogMessage("Unknown player API action: {}.", action);
    }

    const KeelPlayersApi* players{};
    const KeelPlayerInputApi* input{};
    const KeelNativeRuntimeApi* runtime{};
    KeelPlayerConnection remembered{};
};
}

KEELS2_PLUGIN(docs::PlayerAbi)
