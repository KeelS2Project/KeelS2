#ifndef KEELS2_HOST_GAME_ADAPTER_LOADER_H
#define KEELS2_HOST_GAME_ADAPTER_LOADER_H

#include "game_adapter.h"

#include <keels2/platform/dynamic_library.h>

#include <filesystem>
#include <string>

namespace keels2::host
{

class GameAdapterModule final
{
public:
    GameAdapterModule() = default;
    GameAdapterModule(const GameAdapterModule&) = delete;
    GameAdapterModule& operator=(const GameAdapterModule&) = delete;
    ~GameAdapterModule();

    bool Load(
        const std::filesystem::path& directory,
        const char* game,
        const char* platform,
        const GameAdapterHostApi& host,
        std::string& error);
    void Reset() noexcept;
    GameAdapter* Get() const noexcept;
    bool SupportsClientCommands() const noexcept;
    KeelResult CommandCaller(const void* context, std::int32_t& slot) const noexcept;
    KeelResult PlayerAction(const GameEntityIdentity& entity, const KeelPlayerAction& action) const noexcept;
    KeelResult PrintChat(std::int32_t slot, KeelBool broadcast, const char* text) const noexcept;
    std::uint32_t PlayerCapacity() const noexcept;
    KeelResult ReadPlayer(std::int32_t slot, KeelPlayerInfo& player) const noexcept;
    const std::filesystem::path& Path() const noexcept;

private:
    platform::DynamicLibrary library_;
    GameAdapterDestroyFn destroy_{};
    GameAdapterCommandCallerFn command_caller_{};
    GameAdapterPlayerActionFn player_action_{};
    GameAdapterPlayersApi players_{};
    GameAdapterMessagingApi messaging_{};
    GameAdapter* adapter_{};
    std::filesystem::path path_;
};

}

#endif
