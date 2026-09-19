#include "synthetic_adapter.h"

#include <cstdint>
#include <new>
#include <string>
#include <vector>

namespace
{

using namespace keels2::host;

using keels2::host::test::SyntheticAdapter;

GameAdapter* Create(const GameAdapterHostApi* host)
{
    if (!host || host->size != sizeof(GameAdapterHostApi) ||
        host->abi_version != kGameAdapterAbiVersion ||
        !host->begin_command_dispatch || !host->end_command_dispatch)
    {
        return nullptr;
    }

    return new (std::nothrow) SyntheticAdapter(*host);
}

void Destroy(GameAdapter* adapter)
{
    delete adapter;
}

}

extern "C" KEELS2_GAME_ADAPTER_EXPORT std::uint32_t KeelGameAdapter_Query(
    std::uint32_t abi_version,
    keels2::host::GameAdapterProvider* provider)
{
    if (abi_version != keels2::host::kGameAdapterAbiVersion || !provider ||
        provider->size != sizeof(keels2::host::GameAdapterProvider) ||
        provider->abi_version != keels2::host::kGameAdapterAbiVersion)
    {
        return 0;
    }
#if defined(_WIN32)
    constexpr const char* platform = "win64";
#else
    constexpr const char* platform = "linuxsteamrt64";
#endif
    *provider = {
        sizeof(keels2::host::GameAdapterProvider),
        keels2::host::kGameAdapterAbiVersion,
        "synthetic",
        platform,
        &Create,
        &Destroy
    };
    return 1;
}
