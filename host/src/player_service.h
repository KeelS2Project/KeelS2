#ifndef KEELS2_HOST_PLAYER_SERVICE_H
#define KEELS2_HOST_PLAYER_SERVICE_H

#include "player_registry.h"
#include <keels2/player_input.h>

#include <keels2/lifecycle.h>
#include <mutex>

namespace keels2::host
{

class Host;

class GameAdapterModule;

class PlayerService final
{
public:
    PlayerService(Host& host, GameAdapterModule& adapter);
    const KeelPlayersApi& Api() const noexcept;
    const KeelPlayerInputApi& InputApi() const noexcept;
    void OnLifecycle(const KeelLifecycleEvent& event);

private:
    static KeelResult InputEntry(KeelPluginHandle plugin, const KeelPlayerConnection* player, KeelPlayerInput* input);
    static KeelResult GetEntry(KeelPluginHandle plugin, std::int32_t slot, KeelPlayerInfo* player);
    static KeelResult NextEntry(KeelPluginHandle plugin, std::int32_t after, KeelPlayerInfo* player);
    static KeelResult ValidateEntry(KeelPluginHandle plugin,
        const KeelPlayerConnection* connection, KeelPlayerInfo* player);

    static KeelResult Request(KeelPluginHandle plugin, std::int32_t slot, bool next,
        const KeelPlayerConnection* connection, KeelPlayerInfo* player);

    KeelResult Get(KeelPluginHandle plugin, std::int32_t slot, bool next,
        const KeelPlayerConnection* connection, KeelPlayerInfo& player,
        std::unique_lock<std::recursive_mutex>& state_lock, KeelPlayerInput* input = nullptr);

    KeelResult Read(std::int32_t slot, KeelPlayerInfo& player);

    Host& host_;
    GameAdapterModule& adapter_;
    PlayerRegistry registry_;
    KeelPlayersApi api_{};
    KeelPlayerInputApi input_api_{};
};

}

#endif
