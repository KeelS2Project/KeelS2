#include "game_adapter_loader.h"

#include <algorithm>
#include <cstring>

namespace keels2::host
{

namespace
{

bool ValidName(const char* value, std::size_t maximum) noexcept
{
    if (!value || !value[0])
    {
        return false;
    }
    std::size_t length{};
    for (; value[length] && length <= maximum; ++length)
    {
        const unsigned char character = static_cast<unsigned char>(value[length]);
        if (!((character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') || character == '_'))
        {
            return false;
        }
    }
    return length <= maximum;
}

template <typename Function>
Function SymbolFunction(void* address) noexcept
{
    static_assert(sizeof(Function) == sizeof(address));
    Function function{};
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

}

GameAdapterModule::~GameAdapterModule()
{
    Reset();
}

bool GameAdapterModule::Load(
    const std::filesystem::path& directory,
    const char* game,
    const char* platform_name,
    const GameAdapterHostApi& host,
    std::string& error)
{
    Reset();
    if (!ValidName(game, 32) || !ValidName(platform_name, 32) ||
        host.size != sizeof(GameAdapterHostApi) ||
        host.abi_version != kGameAdapterAbiVersion ||
        !host.begin_command_dispatch || !host.end_command_dispatch)
    {
        error = "game adapter load request is invalid";
        return false;
    }
#if defined(_WIN32)
    path_ = directory / (std::string("keels2_game_") + game + ".dll");
#else
    path_ = directory / (std::string("libkeels2_game_") + game + ".so");
#endif
    if (!library_.Open(path_, error))
    {
        error = "game adapter module could not be loaded: " + error;
        path_.clear();
        return false;
    }
    const GameAdapterQueryFn query = SymbolFunction<GameAdapterQueryFn>(
        library_.Symbol(kGameAdapterQuerySymbol));
    if (!query)
    {
        error = "game adapter query export is missing";
        Reset();
        return false;
    }
    GameAdapterProvider provider{};
    provider.size = sizeof(provider);
    provider.abi_version = kGameAdapterAbiVersion;
    try
    {
        if (!query(kGameAdapterAbiVersion, &provider))
        {
            error = "game adapter rejected the provider ABI";
            Reset();
            return false;
        }
    }
    catch (...)
    {
        error = "game adapter query raised an exception";
        Reset();
        return false;
    }
    if (provider.size != sizeof(provider) ||
        provider.abi_version != kGameAdapterAbiVersion || !provider.game ||
        !provider.platform || std::strcmp(provider.game, game) != 0 ||
        std::strcmp(provider.platform, platform_name) != 0 ||
        !provider.create || !provider.destroy)
    {
        error = "game adapter provider metadata is incompatible";
        Reset();
        return false;
    }
    GameAdapter* adapter{};
    try
    {
        adapter = provider.create(&host);
    }
    catch (...)
    {
        error = "game adapter creation raised an exception";
        Reset();
        return false;
    }
    if (!adapter || !adapter->Name() || std::strcmp(adapter->Name(), game) != 0)
    {
        if (adapter)
        {
            try
            {
                provider.destroy(adapter);
            }
            catch (...)
            {
            }
        }
        error = "game adapter instance metadata is incompatible";
        Reset();
        return false;
    }
    adapter_ = adapter;
    destroy_ = provider.destroy;
    command_caller_ = SymbolFunction<GameAdapterCommandCallerFn>(
        library_.Symbol(kGameAdapterCommandCallerSymbol));
    player_action_ = SymbolFunction<GameAdapterPlayerActionFn>(
        library_.Symbol(kGameAdapterPlayerActionSymbol));
    const auto query_players = SymbolFunction<GameAdapterQueryPlayersFn>(
        library_.Symbol(kGameAdapterPlayersSymbol));
    if (query_players)
    {
        GameAdapterPlayersApi players{};
        players.size = sizeof(players);
        players.api_version = kGameAdapterPlayersVersion;
        if (query_players(kGameAdapterPlayersVersion, &players) != KEEL_RESULT_OK ||
            players.size != sizeof(players) || players.api_version != kGameAdapterPlayersVersion ||
            !players.capacity || !players.read)
        {
            error = "game adapter player provider is incompatible";
            Reset();
            return false;
        }
        players_ = players;
    }
    const auto query_messaging = SymbolFunction<GameAdapterQueryMessagingFn>(
        library_.Symbol(kGameAdapterMessagingSymbol));
    if (query_messaging)
    {
        GameAdapterMessagingApi messaging{};
        messaging.size = sizeof(messaging);
        if (query_messaging(kGameAdapterMessagingVersion, &messaging) != KEEL_RESULT_OK ||
            messaging.size != sizeof(messaging) || messaging.api_version != kGameAdapterMessagingVersion || !messaging.chat)
        {
            error = "game adapter messaging provider is incompatible";
            Reset();
            return false;
        }
        messaging_ = messaging;
    }
    error.clear();
    return true;
}

void GameAdapterModule::Reset() noexcept
{
    if (adapter_ && destroy_)
    {
        try
        {
            destroy_(adapter_);
        }
        catch (...)
        {
        }
    }
    adapter_ = nullptr;
    destroy_ = nullptr;
    command_caller_ = nullptr;
    player_action_ = nullptr;
    players_ = {};
    messaging_ = {};
    library_.Close();
    path_.clear();
}

GameAdapter* GameAdapterModule::Get() const noexcept
{
    return adapter_;
}

bool GameAdapterModule::SupportsClientCommands() const noexcept
{
    return command_caller_ != nullptr;
}

KeelResult GameAdapterModule::CommandCaller(const void* context, std::int32_t& slot) const noexcept
{
    slot = -1;
    return command_caller_ ? command_caller_(context, &slot) : KEEL_RESULT_OK;
}

const std::filesystem::path& GameAdapterModule::Path() const noexcept
{
    return path_;
}

KeelResult GameAdapterModule::PrintChat(std::int32_t slot, KeelBool broadcast, const char* text) const noexcept
{
    return messaging_.chat ? messaging_.chat(adapter_, slot, broadcast, text) : KEEL_RESULT_UNSUPPORTED;
}

std::uint32_t GameAdapterModule::PlayerCapacity() const noexcept
{
    return players_.capacity ? players_.capacity() : 0;
}

KeelResult GameAdapterModule::ReadPlayer(std::int32_t slot, KeelPlayerInfo& player) const noexcept
{
    player = {};
    player.size = sizeof(player);
    player.slot = -1;
    player.user_id = -1;
    player.controller_handle = UINT32_MAX;
    player.pawn_handle = UINT32_MAX;
    return players_.read ? players_.read(adapter_, slot, &player) : KEEL_RESULT_UNSUPPORTED;
}

KeelResult GameAdapterModule::PlayerAction(const GameEntityIdentity& entity, const KeelPlayerAction& action) const noexcept
{
    return adapter_ && player_action_ ? player_action_(adapter_, &entity, &action) : KEEL_RESULT_UNSUPPORTED;
}

}
