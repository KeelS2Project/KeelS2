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
    library_.Close();
    path_.clear();
}

GameAdapter* GameAdapterModule::Get() const noexcept
{
    return adapter_;
}

const std::filesystem::path& GameAdapterModule::Path() const noexcept
{
    return path_;
}

}
