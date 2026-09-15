#include "player_registry.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

using keels2::host::PlayerRegistry;

static_assert(std::is_standard_layout_v<KeelPlayerInfo>);
static_assert(std::is_trivially_copyable_v<KeelPlayerInfo>);
static_assert(sizeof(KeelPlayerConnection) == 16);
static_assert(sizeof(KeelPlayerInfo) == 184);
static_assert(sizeof(KeelPlayersApi) == 32);

namespace
{

int Failure(int code, const char* message)
{
    std::fprintf(stderr, "player registry: %s\n", message);
    return code;
}

KeelPlayerInfo Facts(int slot, int user)
{
    KeelPlayerInfo result;
    PlayerRegistry::Clear(result);
    result.slot = slot;
    result.user_id = user;
    result.flags = KEELS2_PLAYER_CONNECTED;
    std::memcpy(result.name, "Player", sizeof("Player"));
    return result;
}

KeelPlayerConnection Connection(const KeelPlayerInfo& player)
{
    KeelPlayerConnection result{};
    result.slot = player.slot;
    result.generation = player.connection;
    return result;
}

bool Empty(const KeelPlayerInfo& player)
{
    return player.size == sizeof(player) && player.slot == -1 && player.user_id == -1 &&
        !player.flags && !player.connection && !player.steam_id && !player.name[0] &&
        player.controller_handle == UINT32_MAX && player.pawn_handle == UINT32_MAX;
}

}

int main()
{
    PlayerRegistry registry(129);
    KeelPlayerInfo player{};
    if (registry.Capacity() != 129 || registry.ValidSlot(-1) || registry.ValidSlot(129) ||
        !registry.ValidSlot(128) || registry.Pending(129, player) || !Empty(player))
    {
        return Failure(1, "slot bounds or empty output contract failed");
    }

    auto facts = Facts(96, 4301);
    if (registry.Update(facts, player) != KEEL_RESULT_OK || player.slot != 96 ||
        player.user_id != 4301 || !player.connection || !registry.Current(Connection(player)))
    {
        return Failure(2, "late lookup conflated slot, user ID, or connection");
    }
    const KeelPlayerInfo queued = player;
    const KeelPlayerConnection first = Connection(player);
    facts.flags |= KEELS2_PLAYER_AUTHENTICATED | KEELS2_PLAYER_ALIVE;
    facts.steam_id = 76561198000000004ull;
    facts.controller_handle = 0x10203040;
    facts.pawn_handle = 0x20304050;
    facts.team = 3;
    if (registry.Update(facts, player) != KEEL_RESULT_OK || player.connection != first.generation ||
        !registry.Current(first) || player.steam_id != facts.steam_id || queued.steam_id != 0 ||
        player.steam_id == queued.steam_id || player.pawn_handle != facts.pawn_handle || player.team != 3)
    {
        return Failure(3, "authentication changed the connection or mutated an earlier snapshot");
    }
    facts.flags &= ~KEELS2_PLAYER_AUTHENTICATED;
    facts.steam_id = 0;
    if (registry.Update(facts, player) != KEEL_RESULT_OK || player.connection != first.generation)
    {
        return Failure(4, "authentication loss changed the connection");
    }

    registry.Disconnected(96);
    if (registry.Current(first) || registry.Update(facts, player) != KEEL_RESULT_NOT_FOUND || !Empty(player))
    {
        return Failure(5, "disconnecting engine data resurrected an invalid connection");
    }
    registry.Missing(96);
    if (registry.Update(facts, player) != KEEL_RESULT_NOT_FOUND || !Empty(player))
    {
        return Failure(6, "an empty lookup cleared the disconnect tombstone");
    }
    if (registry.Connected(96, "Replacement", false) != KEEL_RESULT_OK ||
        !registry.Pending(96, player) || player.user_id != -1 || player.steam_id ||
        !(player.flags & KEELS2_PLAYER_CONNECTING) || std::strcmp(player.name, "Replacement") != 0 ||
        player.connection == first.generation || registry.Current(first))
    {
        return Failure(7, "reconnect reused a generation or trusted an unverified identity");
    }
    const auto connecting = Connection(player);
    registry.Missing(96);
    if (!registry.Pending(96, player) || !registry.Current(connecting) ||
        registry.Update(facts, player) != KEEL_RESULT_OK || player.connection != connecting.generation ||
        registry.Pending(96, player) || !Empty(player))
    {
        return Failure(8, "pending connection did not survive initial player-info unavailability");
    }

    facts.user_id = 9003;
    if (registry.Update(facts, player) != KEEL_RESULT_OK || registry.Current(connecting))
    {
        return Failure(9, "slot reuse with a different server user ID preserved a stale connection");
    }
    const auto replacement = Connection(player);
    registry.Missing(96);
    if (registry.Current(replacement) || registry.Update(facts, player) != KEEL_RESULT_OK ||
        player.connection == replacement.generation)
    {
        return Failure(10, "missing native player retained an occupied slot");
    }
    const auto human = Connection(player);
    facts.flags = KEELS2_PLAYER_CONNECTED | KEELS2_PLAYER_BOT;
    if (registry.Update(facts, player) != KEEL_RESULT_OK || registry.Current(human))
    {
        return Failure(11, "bot replacement kept a human connection");
    }
    const auto bot = Connection(player);
    facts.flags |= KEELS2_PLAYER_SOURCE_TV;
    if (registry.Update(facts, player) != KEEL_RESULT_OK || registry.Current(bot) ||
        !(player.flags & KEELS2_PLAYER_SOURCE_TV))
    {
        return Failure(12, "SourceTV state was lost or confused with a player");
    }

    const auto current = Connection(player);
    facts.name[0] = 'X';
    std::memset(facts.name, 'X', sizeof(facts.name));
    if (registry.Update(facts, player) != KEEL_RESULT_INVALID_ARGUMENT || !Empty(player) ||
        !registry.Current(current))
    {
        return Failure(13, "malformed native facts changed the connection or leaked output");
    }
    facts = Facts(129, 15);
    if (registry.Update(facts, player) != KEEL_RESULT_INVALID_ARGUMENT || !Empty(player) ||
        registry.Connected(-1, "invalid", false) != KEEL_RESULT_INVALID_ARGUMENT)
    {
        return Failure(14, "out-of-range mutation was accepted");
    }
    facts = Facts(128, 16);
    facts.flags |= KEELS2_PLAYER_AUTHENTICATED;
    if (registry.Update(facts, player) != KEEL_RESULT_INVALID_ARGUMENT || !Empty(player))
    {
        return Failure(15, "authenticated state without a Steam identity was accepted");
    }
    facts.steam_id = 76561198000000004ull;
    facts.flags |= KEELS2_PLAYER_BOT;
    if (registry.Update(facts, player) != KEEL_RESULT_INVALID_ARGUMENT || !Empty(player))
    {
        return Failure(16, "bot facts were accepted as an authenticated player");
    }
    return 0;
}
