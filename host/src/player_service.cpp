#include "player_service.h"

#include "game_adapter_loader.h"
#include "host.h"
#include "lifecycle_service.h"

namespace keels2::host
{

PlayerService::PlayerService(Host& host, GameAdapterModule& adapter)
    : host_(host), adapter_(adapter), registry_(adapter.PlayerCapacity())
{
    api_.size = sizeof(api_);
    api_.api_version = KEELS2_PLAYERS_API_VERSION;
    api_.get_player = &GetEntry;
    api_.get_next_player = &NextEntry;
    api_.validate_connection = &ValidateEntry;
}

const KeelPlayersApi& PlayerService::Api() const noexcept
{
    return api_;
}

KeelResult PlayerService::GetEntry(KeelPluginHandle plugin, std::int32_t slot, KeelPlayerInfo* player)
{
    return Request(plugin, slot, false, nullptr, player);
}

KeelResult PlayerService::NextEntry(KeelPluginHandle plugin, std::int32_t after, KeelPlayerInfo* player)
{
    return Request(plugin, after, true, nullptr, player);
}

KeelResult PlayerService::ValidateEntry(KeelPluginHandle plugin,
    const KeelPlayerConnection* connection, KeelPlayerInfo* player)
{
    return Request(plugin, connection ? connection->slot : -1, false, connection, player);
}

KeelResult PlayerService::Request(KeelPluginHandle plugin, std::int32_t slot, bool next,
    const KeelPlayerConnection* connection, KeelPlayerInfo* player)
{
    if (!player)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    const bool valid_size = player->size == sizeof(*player);
    PlayerRegistry::Clear(*player);
    if (!valid_size || (connection && (connection->reserved || !connection->generation)))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        Host& host = Host::Instance();
        std::unique_lock lock(host.state_mutex_);
        return host.players_ ? host.players_->Get(plugin, slot, next, connection, *player, lock) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        PlayerRegistry::Clear(*player);
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult PlayerService::Get(KeelPluginHandle plugin, std::int32_t slot, bool next,
    const KeelPlayerConnection* connection, KeelPlayerInfo& player,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    if ((!next && !registry_.ValidSlot(slot)) ||
        (next && (slot < -1 || slot >= static_cast<std::int32_t>(registry_.Capacity()))))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    PluginRecord* owner = host_.PluginByHandle(plugin);
    const bool cleanup = owner && owner->unload_callback_active;
    if (!cleanup && (!host_.accepting_resources_ || !owner || !owner->accepting_resources || owner->cleanup_pending ||
        (owner->state != PluginState::loading && owner->state != PluginState::loaded) ||
        (owner->transitioning && !owner->loading)))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!host_.adapter_->IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    if (owner->active_native_operations == UINT32_MAX)
    {
        return KEEL_RESULT_BUSY;
    }
    ++owner->active_native_operations;
    struct ActiveOperation
    {
        std::uint32_t& count;
        std::unique_lock<std::recursive_mutex>& lock;
        ~ActiveOperation()
        {
            if (!lock.owns_lock())
            {
                lock.lock();
            }
            --count;
        }
    } operation{owner->active_native_operations, state_lock};
    const KeelResult tracking = cleanup ? KEEL_RESULT_OK : host_.lifecycle_->EnsurePlayerTracking();
    if (tracking != KEEL_RESULT_OK)
    {
        return tracking;
    }
    state_lock.unlock();
    if (!next)
    {
        const KeelResult result = Read(slot, player);
        if (result == KEEL_RESULT_OK && connection && player.connection != connection->generation)
        {
            PlayerRegistry::Clear(player);
            return KEEL_RESULT_NOT_FOUND;
        }
        return result;
    }
    for (++slot; slot < static_cast<std::int32_t>(registry_.Capacity()); ++slot)
    {
        const KeelResult result = Read(slot, player);
        if (result != KEEL_RESULT_NOT_FOUND)
        {
            return result;
        }
    }
    return KEEL_RESULT_NOT_FOUND;
}

KeelResult PlayerService::Read(std::int32_t slot, KeelPlayerInfo& player)
{
    KeelPlayerInfo facts{};
    const KeelResult result = adapter_.ReadPlayer(slot, facts);
    if (result == KEEL_RESULT_OK)
    {
        return registry_.Update(facts, player);
    }
    PlayerRegistry::Clear(player);
    if (result == KEEL_RESULT_NOT_FOUND)
    {
        registry_.Missing(slot);
        if (registry_.Pending(slot, player))
        {
            return KEEL_RESULT_OK;
        }
    }
    return result;
}

void PlayerService::OnLifecycle(const KeelLifecycleEvent& event)
{
    std::scoped_lock lock(host_.state_mutex_);
    if (!host_.adapter_->IsGameThread())
    {
        return;
    }
    if (event.type == KEELS2_LIFECYCLE_CLIENT_CONNECTED && event.payload_size == sizeof(KeelLifecycleClientConnected))
    {
        const auto& data = *static_cast<const KeelLifecycleClientConnected*>(event.payload);
        if (data.size == sizeof(data) && !data.reserved && data.name &&
            (data.fake_player == KEEL_FALSE || data.fake_player == KEEL_TRUE))
        {
            registry_.Connected(data.slot, data.name, data.fake_player == KEEL_TRUE);
        }
    }
    if (event.type == KEELS2_LIFECYCLE_CLIENT_DISCONNECTING &&
        event.payload_size == sizeof(KeelLifecycleClientDisconnecting))
    {
        const auto& data = *static_cast<const KeelLifecycleClientDisconnecting*>(event.payload);
        if (data.size == sizeof(data) && !data.reserved)
        {
            registry_.Disconnected(data.slot);
        }
    }
}

}
