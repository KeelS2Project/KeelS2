#ifndef KEELS2_HOST_PLAYER_SERVICE_H
#define KEELS2_HOST_PLAYER_SERVICE_H

#include "player_registry.h"

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
    void OnLifecycle(const KeelLifecycleEvent& event);

private:
    static KeelResult GetEntry(KeelPluginHandle plugin, std::int32_t slot, KeelPlayerInfo* player);
    static KeelResult NextEntry(KeelPluginHandle plugin, std::int32_t after, KeelPlayerInfo* player);
    static KeelResult ValidateEntry(KeelPluginHandle plugin,
        const KeelPlayerConnection* connection, KeelPlayerInfo* player);
    static KeelResult Request(KeelPluginHandle plugin, std::int32_t slot, bool next,
        const KeelPlayerConnection* connection, KeelPlayerInfo* player);
    KeelResult Get(KeelPluginHandle plugin, std::int32_t slot, bool next,
        const KeelPlayerConnection* connection, KeelPlayerInfo& player,
        std::unique_lock<std::recursive_mutex>& state_lock);
    KeelResult Read(std::int32_t slot, KeelPlayerInfo& player);

    Host& host_;
    GameAdapterModule& adapter_;
    PlayerRegistry registry_;
    KeelPlayersApi api_{};
};

}

#endif
