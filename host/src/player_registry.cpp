#include "player_registry.h"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace keels2::host
{

PlayerRegistry::PlayerRegistry(std::uint32_t capacity)
{
    if (!capacity || capacity > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
    {
        throw std::invalid_argument("player capacity is outside the slot representation");
    }
    entries_.resize(capacity);
}

std::uint32_t PlayerRegistry::Capacity() const noexcept
{
    return static_cast<std::uint32_t>(entries_.size());
}

bool PlayerRegistry::ValidSlot(std::int32_t slot) const noexcept
{
    return slot >= 0 && static_cast<std::uint32_t>(slot) < Capacity();
}

void PlayerRegistry::Clear(KeelPlayerInfo& player) noexcept
{
    player = {};
    player.size = sizeof(player);
    player.slot = -1;
    player.user_id = -1;
    player.controller_handle = UINT32_MAX;
    player.pawn_handle = UINT32_MAX;
}

std::uint64_t PlayerRegistry::NextGeneration() noexcept
{
    if (!next_generation_)
    {
        return 0;
    }
    return next_generation_++;
}

KeelResult PlayerRegistry::Connected(std::int32_t slot, const char* name, bool bot)
{
    if (!ValidSlot(slot) || !name)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    Entry& entry = entries_[static_cast<std::uint32_t>(slot)];
    entry = {};
    Clear(entry.player);
    entry.player.slot = slot;
    entry.player.connection = NextGeneration();
    if (!entry.player.connection)
    {
        entry.disconnecting = true;
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    entry.player.flags = KEELS2_PLAYER_CONNECTED | KEELS2_PLAYER_CONNECTING;
    if (bot)
    {
        entry.player.flags |= KEELS2_PLAYER_BOT;
    }
    std::size_t length{};
    while (length + 1 < sizeof(entry.player.name) && name[length])
    {
        ++length;
    }
    std::memcpy(entry.player.name, name, length);
    entry.player.name[length] = '\0';
    entry.pending = true;
    return KEEL_RESULT_OK;
}

void PlayerRegistry::Disconnected(std::int32_t slot) noexcept
{
    if (ValidSlot(slot))
    {
        Entry& entry = entries_[static_cast<std::uint32_t>(slot)];
        entry = {};
        entry.disconnecting = true;
    }
}

void PlayerRegistry::Missing(std::int32_t slot) noexcept
{
    if (ValidSlot(slot))
    {
        Entry& entry = entries_[static_cast<std::uint32_t>(slot)];
        if (!entry.pending && !entry.disconnecting)
        {
            entry = {};
        }
    }
}

bool PlayerRegistry::ValidFacts(const KeelPlayerInfo& facts) noexcept
{
    constexpr std::uint32_t flags = KEELS2_PLAYER_CONNECTED | KEELS2_PLAYER_CONNECTING |
        KEELS2_PLAYER_AUTHENTICATED | KEELS2_PLAYER_BOT | KEELS2_PLAYER_SOURCE_TV | KEELS2_PLAYER_ALIVE;
    if (facts.size != sizeof(facts) || facts.reserved || (facts.flags & ~flags) ||
        !(facts.flags & KEELS2_PLAYER_CONNECTED) || facts.user_id < -1 ||
        !std::memchr(facts.name, '\0', sizeof(facts.name)))
    {
        return false;
    }
    const bool authenticated = (facts.flags & KEELS2_PLAYER_AUTHENTICATED) != 0;
    if (authenticated != (facts.steam_id != 0) ||
        (authenticated && (facts.flags & (KEELS2_PLAYER_BOT | KEELS2_PLAYER_SOURCE_TV))))
    {
        return false;
    }
    return facts.user_id >= 0 || (facts.flags & KEELS2_PLAYER_CONNECTING);
}

KeelResult PlayerRegistry::Update(const KeelPlayerInfo& facts, KeelPlayerInfo& player)
{
    const KeelPlayerInfo current = facts;
    Clear(player);
    if (!ValidSlot(current.slot) || !ValidFacts(current))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    Entry& entry = entries_[static_cast<std::uint32_t>(current.slot)];
    if (entry.disconnecting)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    constexpr std::uint32_t identity_flags = KEELS2_PLAYER_BOT | KEELS2_PLAYER_SOURCE_TV;
    const bool different_user = entry.player.user_id >= 0 && current.user_id >= 0 &&
        entry.player.user_id != current.user_id;
    const bool different_kind = !entry.pending &&
        ((entry.player.flags ^ current.flags) & identity_flags);
    std::uint64_t generation = entry.player.connection;
    if (!generation || different_user || different_kind)
    {
        generation = NextGeneration();
    }
    if (!generation)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    entry.player = current;
    entry.player.connection = generation;
    entry.pending = (current.flags & KEELS2_PLAYER_CONNECTING) != 0;
    player = entry.player;
    return KEEL_RESULT_OK;
}

bool PlayerRegistry::Pending(std::int32_t slot, KeelPlayerInfo& player) const noexcept
{
    Clear(player);
    if (!ValidSlot(slot))
    {
        return false;
    }
    const Entry& entry = entries_[static_cast<std::uint32_t>(slot)];
    if (!entry.pending || entry.disconnecting || !entry.player.connection)
    {
        return false;
    }
    player = entry.player;
    return true;
}

bool PlayerRegistry::Current(const KeelPlayerConnection& connection) const noexcept
{
    if (!ValidSlot(connection.slot) || connection.reserved || !connection.generation)
    {
        return false;
    }
    const Entry& entry = entries_[static_cast<std::uint32_t>(connection.slot)];
    return !entry.disconnecting && entry.player.connection == connection.generation;
}

}
