#include "player_service_contract.h"

#include <cstring>
#include <thread>

namespace
{

bool Empty(const KeelPlayerInfo& player)
{
    return player.size == sizeof(player) && player.slot == -1 && player.user_id == -1 &&
        !player.connection && !player.flags && !player.steam_id && !player.name[0] &&
        player.controller_handle == UINT32_MAX && player.pawn_handle == UINT32_MAX;
}

KeelPlayerConnection Connection(const KeelPlayerInfo& player)
{
    KeelPlayerConnection connection{};
    connection.slot = player.slot;
    connection.generation = player.connection;
    return connection;
}

}

bool PlayerServiceContract::Check(keels2::Context& context, const char* stage)
{
    const KeelPluginHandle plugin = context.PluginHandle();
    KeelPlayerInfo player{};
    player.size = sizeof(player);
    keels2::PlayerInfo native;
    if (std::strcmp(stage, "late") == 0)
    {
        const void* service = reinterpret_cast<const void*>(1);
        if (context.QueryService(KEELS2_PLAYERS_SERVICE_NAME, 2, &service) != KEEL_RESULT_INCOMPATIBLE || service ||
            context.QueryService(KEELS2_PLAYERS_SERVICE_NAME, 1, &service) != KEEL_RESULT_OK || !service)
        {
            return false;
        }
        api_ = static_cast<const KeelPlayersApi*>(service);
        if (api_->size != sizeof(*api_) || api_->api_version != 1 ||
            api_->get_player(plugin, 3, &player) != KEEL_RESULT_OK || player.slot != 3 ||
            player.user_id != 4301 || !player.connection || player.steam_id ||
            player.flags != KEELS2_PLAYER_CONNECTED || std::strcmp(player.name, "Late player") != 0)
        {
            return false;
        }
        if (runtime_.Connect(context) != KEEL_RESULT_OK || runtime_.CheckGameThread() != KEEL_RESULT_OK ||
            service_.Connect(context) != KEEL_RESULT_OK ||
            service_.Get(CPlayerSlot(3), native) != KEEL_RESULT_OK ||
            native.slot.Get() != 3 || native.user_id != 4301 ||
            native.connection != player.connection || native.authenticated ||
            native.steam_id.ConvertToUint64() || std::strcmp(native.name.String(), "Late player") != 0)
        {
            return false;
        }
        const auto connection = native.Connection();
        if (service_.Validate(connection, native) != KEEL_RESULT_OK ||
            native.connection != connection.generation ||
            service_.Get(CPlayerSlot(-1), native) != KEEL_RESULT_INVALID_ARGUMENT ||
            native.slot.Get() != -1 || native.connection || native.name.Length())
        {
            return false;
        }
        if (service_.GetByUserId(4301, native) != KEEL_RESULT_OK || native.slot.Get() != 3 ||
            native.user_id != 4301 || native.connection != connection.generation ||
            service_.GetByUserId(3, native) != KEEL_RESULT_NOT_FOUND || native.user_id != -1 ||
            service_.GetByUserId(-1, native) != KEEL_RESULT_INVALID_ARGUMENT || native.connection)
        {
            return false;
        }
        first_ = player;
        current_ = player;
        if (api_->get_next_player(plugin, -1, &player) != KEEL_RESULT_OK ||
            player.slot != 3 || player.connection != first_.connection ||
            api_->get_next_player(plugin, 3, &player) != KEEL_RESULT_NOT_FOUND || !Empty(player) ||
            api_->get_player(plugin, -1, &player) != KEEL_RESULT_INVALID_ARGUMENT || !Empty(player) ||
            api_->get_player(plugin, INT32_MAX, &player) != KEEL_RESULT_INVALID_ARGUMENT || !Empty(player) ||
            api_->get_player(0, 3, &player) != KEEL_RESULT_NOT_READY || !Empty(player))
        {
            return false;
        }
        KeelResult wrong_thread{};
        KeelResult wrong_lookup_thread{};
        std::thread worker([&] {
            wrong_thread = api_->get_player(plugin, 3, &player);
            wrong_lookup_thread = service_.GetByUserId(4301, native);
        });
        worker.join();
        return wrong_thread == KEEL_RESULT_WRONG_THREAD && Empty(player) &&
            wrong_lookup_thread == KEEL_RESULT_WRONG_THREAD && native.user_id == -1 && !native.connection;
    }
    if (!api_)
    {
        return false;
    }
    if (std::strcmp(stage, "native") == 0)
    {
        if (runtime_.PrintToConsole(CPlayerSlot(3), "100% {literal}; quit\n") != KEEL_RESULT_OK ||
            runtime_.PrintToConsole(CPlayerSlot(-1), "invalid") != KEEL_RESULT_INVALID_ARGUMENT ||
            runtime_.PrintToConsole(CPlayerSlot(2), "absent") != KEEL_RESULT_NOT_FOUND ||
            runtime_.PrintToChat(CPlayerSlot(3), "unavailable") != KEEL_RESULT_NOT_FOUND ||
            runtime_.PrintToChatAll(nullptr) != KEEL_RESULT_INVALID_ARGUMENT)
        {
            return false;
        }
        KeelResult check{};
        KeelResult print{};
        std::thread worker([&] {
            check = runtime_.CheckGameThread();
            print = runtime_.PrintToConsole(CPlayerSlot(3), "wrong thread");
        });
        worker.join();
        return check == KEEL_RESULT_WRONG_THREAD && print == KEEL_RESULT_WRONG_THREAD;
    }
    if (std::strcmp(stage, "auth") == 0)
    {
        const auto connection = Connection(first_);
        return api_->validate_connection(plugin, &connection, &player) == KEEL_RESULT_OK &&
            player.connection == first_.connection && player.steam_id == 76561198000000004ull &&
            (player.flags & KEELS2_PLAYER_AUTHENTICATED) && first_.steam_id == 0;
    }
    if (std::strcmp(stage, "disconnect") == 0)
    {
        const auto connection = Connection(first_);
        return api_->get_player(plugin, 3, &player) == KEEL_RESULT_NOT_FOUND && Empty(player) &&
            api_->validate_connection(plugin, &connection, &player) == KEEL_RESULT_NOT_FOUND && Empty(player) &&
            service_.GetByUserId(4301, native) == KEEL_RESULT_NOT_FOUND && native.user_id == -1 && !native.connection;
    }
    if (std::strcmp(stage, "reconnect") == 0 || std::strcmp(stage, "reuse") == 0)
    {
        const auto previous = Connection(current_);
        if (api_->get_player(plugin, 3, &player) != KEEL_RESULT_OK ||
            player.connection == previous.generation || !player.connection ||
            player.user_id != (std::strcmp(stage, "reuse") == 0 ? 9003 : 4301))
        {
            return false;
        }
        current_ = player;
        if (service_.GetByUserId(current_.user_id, native) != KEEL_RESULT_OK ||
            native.slot.Get() != 3 || native.connection != current_.connection ||
            (current_.user_id != 4301 && service_.GetByUserId(4301, native) != KEEL_RESULT_NOT_FOUND))
        {
            return false;
        }
        return api_->validate_connection(plugin, &previous, &player) == KEEL_RESULT_NOT_FOUND && Empty(player);
    }
    if (std::strcmp(stage, "throw") == 0)
    {
        return api_->get_player(plugin, 3, &player) == KEEL_RESULT_ENGINE_FAILURE && Empty(player);
    }
    return false;
}

bool PlayerServiceContract::Unloaded(KeelPluginHandle plugin) const
{
    KeelPlayerInfo player{};
    player.size = sizeof(player);
    if (!api_)
    {
        return true;
    }
    if (runtime_.CheckGameThread() != KEEL_RESULT_OK ||
        runtime_.PrintToConsole(CPlayerSlot(3), "unloading") != KEEL_RESULT_NOT_READY ||
        api_->get_player(plugin, 3, &player) != KEEL_RESULT_OK ||
        player.connection != current_.connection || player.user_id != 9003)
    {
        return false;
    }
    keels2::PlayerInfo native;
    if (service_.Get(CPlayerSlot(3), native) != KEEL_RESULT_OK ||
        native.connection != current_.connection)
    {
        return false;
    }
    KeelResult result{};
    std::thread worker([&] { result = api_->get_player(plugin, 3, &player); });
    worker.join();
    return result == KEEL_RESULT_WRONG_THREAD && Empty(player);
}
