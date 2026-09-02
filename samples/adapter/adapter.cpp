#include <keels2/game_adapter.hpp>

#include <cstdint>
#include <new>
#include <string>
#include <vector>

namespace
{

using namespace keels2::host;

class SyntheticAdapter final : public GameAdapter
{
public:
    explicit SyntheticAdapter(const GameAdapterHostApi& host)
        : host_(host)
    {
    }

    const char* Name() const override
    {
        return "synthetic";
    }

    bool Start(
        KeelCreateInterfaceFn,
        KeelCreateInterfaceFn,
        const KeelHostCompatibilityInfo&,
        std::string& error) override
    {
        started_ = true;
        error.clear();
        return true;
    }

    bool CompleteStartup(std::string& error) override
    {
        if (!started_)
        {
            error = "synthetic adapter is not started";
            return false;
        }
        error.clear();
        return true;
    }

    void Stop() noexcept override
    {
        started_ = false;
    }

    bool IsGameThread() const noexcept override
    {
        return started_;
    }

    KeelResult QueryInterface(
        KeelSource2Capability,
        KeelSource2InterfaceInfo&) const noexcept override
    {
        return KEEL_RESULT_NOT_FOUND;
    }

    KeelResult QueryNamedInterface(
        KeelSource2Factory,
        const char*,
        KeelSource2InterfaceInfo&) override
    {
        return KEEL_RESULT_NOT_FOUND;
    }

    std::vector<GameInterfaceSnapshot> InterfaceSnapshots() const override
    {
        return {};
    }

    KeelResult EnableLifecycleEvent(
        KeelLifecycleEventType,
        const KeelHookApi&,
        KeelPluginHandle,
        GameLifecycleCallback,
        void*,
        std::string&) override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult InitializeSource2Callbacks(
        const KeelHookApi&,
        KeelPluginHandle,
        GameSource2Callback,
        void*,
        std::string&) override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    void ShutdownSource2Callbacks() noexcept override
    {
    }

    KeelResult ListenForGameEvent(const char*, std::string&) override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    bool RegisterCommand(
        const GameCommandSpec&,
        GameCommandHandle& command,
        std::string& error) override
    {
        command = 0;
        error = "synthetic adapter does not provide commands";
        return false;
    }

    void UnregisterCommand(GameCommandHandle) noexcept override
    {
    }

    KeelResult CreateConVar(
        const KeelConVarSpec&,
        GameConVarCallback,
        GameNativeConVarCallback,
        void*,
        GameConVarHandle& convar,
        void** native_convar,
        std::string&) override
    {
        convar = 0;
        if (native_convar)
        {
            *native_convar = nullptr;
        }
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult FindConVar(
        const char*,
        KeelConVarType,
        GameConVarHandle& convar,
        void** native_convar,
        std::string&) override
    {
        convar = 0;
        if (native_convar)
        {
            *native_convar = nullptr;
        }
        return KEEL_RESULT_UNSUPPORTED;
    }

    void ReleaseConVar(GameConVarHandle) noexcept override
    {
    }

    KeelResult ReadConVar(
        GameConVarHandle,
        std::int32_t,
        KeelConVarValue&) const noexcept override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult QueueConVarSet(
        GameConVarHandle,
        std::int32_t,
        const KeelConVarValue&) noexcept override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult DescribeConVar(
        GameConVarHandle,
        KeelConVarInfo&) const noexcept override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult ResolveSchemaField(
        const KeelSchemaFieldSpec&,
        GameSchemaField&,
        std::string&) override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult FindEntityByIndex(
        std::int32_t,
        GameEntityIdentity&,
        std::string&) override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult FindEntityBySource2Handle(
        std::uint32_t,
        GameEntityIdentity&,
        std::string&) override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult ValidateEntity(
        const GameEntityIdentity&,
        std::string&) override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

    KeelResult ReadEntityField(
        const GameEntityIdentity&,
        const GameSchemaField&,
        void*,
        std::uint32_t,
        std::string&) override
    {
        return KEEL_RESULT_UNSUPPORTED;
    }

private:
    GameAdapterHostApi host_{};
    bool started_{};
};

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
