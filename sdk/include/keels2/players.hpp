#ifndef KEELS2_PLAYERS_HPP
#define KEELS2_PLAYERS_HPP

#include <keels2/players.h>
#include <keels2/plugin.hpp>

#include <entityhandle.h>
#include <playerslot.h>
#include <steam/steamclientpublic.h>
#include <tier1/utlstring.h>

#include <cstring>

namespace keels2
{

struct PlayerConnection
{
    CPlayerSlot slot{-1};
    uint64 generation{};
};

struct PlayerInfo
{
    CPlayerSlot slot{-1};
    int user_id{-1};
    uint64 connection{};
    CSteamID steam_id;
    CUtlString name;
    int team{};
    CEntityHandle controller;
    CEntityHandle pawn;
    bool connected{};
    bool connecting{};
    bool authenticated{};
    bool bot{};
    bool source_tv{};
    bool alive{};

    PlayerConnection Connection() const
    {
        PlayerConnection result;
        result.slot = slot;
        result.generation = connection;
        return result;
    }
};

namespace players
{

class Service final
{
public:
    KeelResult Connect(const Context& context) noexcept
    {
        api_ = nullptr;
        context_.reset();
        const void* service{};
        const KeelResult result = context.QueryService(
            KEELS2_PLAYERS_SERVICE_NAME, KEELS2_PLAYERS_API_VERSION, &service);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        const auto* api = static_cast<const KeelPlayersApi*>(service);
        if (!api || api->size != sizeof(*api) || api->api_version != KEELS2_PLAYERS_API_VERSION ||
            !api->get_player || !api->get_next_player || !api->validate_connection)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        context_ = context.State();
        api_ = api;
        return KEEL_RESULT_OK;
    }

    explicit operator bool() const noexcept
    {
        return api_ && context_ && context_->native_access.load(std::memory_order_acquire);
    }

    KeelResult Get(CPlayerSlot slot, PlayerInfo& player) const
    {
        KeelPlayerInfo info{};
        info.size = sizeof(info);
        const KeelResult result = *this ? api_->get_player(context_->plugin, slot.Get(), &info) : KEEL_RESULT_NOT_READY;
        return Copy(result, info, player);
    }

    KeelResult Next(CPlayerSlot after, PlayerInfo& player) const
    {
        KeelPlayerInfo info{};
        info.size = sizeof(info);
        const KeelResult result = *this ? api_->get_next_player(context_->plugin, after.Get(), &info) : KEEL_RESULT_NOT_READY;
        return Copy(result, info, player);
    }

    KeelResult Validate(const PlayerConnection& connection, PlayerInfo& player) const
    {
        KeelPlayerConnection expected{};
        expected.slot = connection.slot.Get();
        expected.generation = connection.generation;
        KeelPlayerInfo info{};
        info.size = sizeof(info);
        const KeelResult result = *this ? api_->validate_connection(context_->plugin, &expected, &info) : KEEL_RESULT_NOT_READY;
        return Copy(result, info, player);
    }

private:
    static KeelResult Copy(KeelResult result, const KeelPlayerInfo& info, PlayerInfo& player)
    {
        player = {};
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        if (info.size != sizeof(info) || info.reserved || info.slot < 0 || !info.connection ||
            !std::memchr(info.name, '\0', sizeof(info.name)))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        player.slot = CPlayerSlot(info.slot);
        player.user_id = info.user_id;
        player.connection = info.connection;
        player.steam_id = CSteamID(static_cast<uint64>(info.steam_id));
        player.name = info.name;
        player.team = info.team;
        player.controller = CEntityHandle(info.controller_handle);
        player.pawn = CEntityHandle(info.pawn_handle);
        player.connected = (info.flags & KEELS2_PLAYER_CONNECTED) != 0;
        player.connecting = (info.flags & KEELS2_PLAYER_CONNECTING) != 0;
        player.authenticated = (info.flags & KEELS2_PLAYER_AUTHENTICATED) != 0;
        player.bot = (info.flags & KEELS2_PLAYER_BOT) != 0;
        player.source_tv = (info.flags & KEELS2_PLAYER_SOURCE_TV) != 0;
        player.alive = (info.flags & KEELS2_PLAYER_ALIVE) != 0;
        return KEEL_RESULT_OK;
    }

    std::shared_ptr<detail::ContextState> context_;
    const KeelPlayersApi* api_{};
};

}
}

#endif
