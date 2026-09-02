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
    const std::filesystem::path& Path() const noexcept;

private:
    platform::DynamicLibrary library_;
    GameAdapterDestroyFn destroy_{};
    GameAdapter* adapter_{};
    std::filesystem::path path_;
};

}

#endif
