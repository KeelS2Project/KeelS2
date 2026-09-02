#include "game_adapter_loader.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace
{

std::uint32_t dispatches{};

std::uint32_t Begin() noexcept
{
    ++dispatches;
    return 1;
}

void End() noexcept
{
    --dispatches;
}

}

int main(int argument_count, char** arguments)
{
    if (argument_count != 2)
    {
        return 1;
    }
#if defined(_WIN32)
    constexpr const char* platform = "win64";
#else
    constexpr const char* platform = "linuxsteamrt64";
#endif
    const keels2::host::GameAdapterHostApi host{
        sizeof(keels2::host::GameAdapterHostApi),
        keels2::host::kGameAdapterAbiVersion,
        &Begin,
        &End
    };
    keels2::host::GameAdapterModule module;
    std::string error;
    if (!module.Load(arguments[1], "synthetic", platform, host, error) ||
        !module.Get() || std::string(module.Get()->Name()) != "synthetic" ||
        module.Path().parent_path() != std::filesystem::path(arguments[1]))
    {
        return 2;
    }
    KeelHostCompatibilityInfo compatibility{};
    if (!module.Get()->Start(nullptr, nullptr, compatibility, error) ||
        !module.Get()->CompleteStartup(error) || !module.Get()->IsGameThread())
    {
        return 3;
    }
    module.Get()->Stop();
    if (module.Get()->IsGameThread())
    {
        return 4;
    }
    module.Reset();
    if (module.Get() || !module.Path().empty() || dispatches != 0)
    {
        return 5;
    }
    auto invalid_host = host;
    ++invalid_host.abi_version;
    if (module.Load(arguments[1], "synthetic", platform, invalid_host, error) ||
        module.Get())
    {
        return 6;
    }
    if (module.Load(arguments[1], "synthetic", "wrong", host, error) ||
        module.Get())
    {
        return 7;
    }
    if (module.Load(arguments[1], "../synthetic", platform, host, error) ||
        module.Get())
    {
        return 8;
    }
    return 0;
}
