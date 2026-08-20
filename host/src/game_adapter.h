#ifndef KEELS2_HOST_GAME_ADAPTER_H
#define KEELS2_HOST_GAME_ADAPTER_H

#include <keels2/bootstrap_api.h>
#include <keels2/convar.h>
#include <keels2/keelhook.h>
#include <keels2/lifecycle.h>
#include <keels2/source2.h>
#include <keels2/source2_callbacks.h>

#include <cstdint>
#include <memory>
#include <string>

namespace keels2::host
{

using GameCommandHandle = std::uint64_t;
using GameConVarHandle = std::uint64_t;

struct GameCommandInvocation
{
    std::uint32_t argument_count;
    const char* const* arguments;
    const void* context;
    const void* command;
};

using GameCommandCallback = void (*)(const GameCommandInvocation& invocation, void* user_data);
using GameLifecycleCallback = void (*)(const KeelLifecycleEvent& event, void* user_data);
using GameSource2Callback = KeelBool (*)(
    KeelSource2CallbackEvent& event,
    void* user_data);
using GameConVarCallback = void (*)(
    std::int32_t slot,
    const KeelConVarValue& new_value,
    const KeelConVarValue& old_value,
    void* user_data);
using GameNativeConVarCallback = void (*)(
    void* convar,
    std::int32_t slot,
    const void* new_value,
    const void* old_value,
    void* user_data);

bool BeginGameCommandDispatch() noexcept;
void EndGameCommandDispatch() noexcept;

struct GameCommandSpec
{
    const char* name;
    const char* description;
    std::uint64_t flags;
    GameCommandCallback callback;
    void* user_data;
};

class GameAdapter
{
public:
    virtual ~GameAdapter() = default;
    virtual const char* Name() const = 0;
    virtual bool Start(
        KeelCreateInterfaceFn engine_factory,
        KeelCreateInterfaceFn server_factory,
        const KeelHostCompatibilityInfo& compatibility,
        std::string& error) = 0;
    virtual bool CompleteStartup(std::string& error) = 0;
    virtual void Stop() noexcept = 0;
    virtual KeelResult QueryInterface(
        KeelSource2Capability capability,
        KeelSource2InterfaceInfo& info) const noexcept = 0;
    virtual KeelResult EnableLifecycleEvent(
        KeelLifecycleEventType event,
        const KeelHookApi& hooks,
        KeelPluginHandle owner,
        GameLifecycleCallback callback,
        void* user_data,
        std::string& error) = 0;
    virtual KeelResult InitializeSource2Callbacks(
        const KeelHookApi& hooks,
        KeelPluginHandle owner,
        GameSource2Callback callback,
        void* user_data,
        std::string& error) = 0;
    virtual void ShutdownSource2Callbacks() noexcept = 0;
    virtual KeelResult ListenForGameEvent(
        const char* name,
        std::string& error) = 0;
    virtual bool RegisterCommand(const GameCommandSpec& spec, GameCommandHandle& command, std::string& error) = 0;
    virtual void UnregisterCommand(GameCommandHandle command) noexcept = 0;
    virtual KeelResult CreateConVar(
        const KeelConVarSpec& spec,
        GameConVarCallback callback,
        GameNativeConVarCallback native_callback,
        void* user_data,
        GameConVarHandle& convar,
        void** native_convar,
        std::string& error) = 0;
    virtual KeelResult FindConVar(
        const char* name,
        KeelConVarType expected_type,
        GameConVarHandle& convar,
        void** native_convar,
        std::string& error) = 0;
    virtual void ReleaseConVar(GameConVarHandle convar) noexcept = 0;
    virtual KeelResult ReadConVar(
        GameConVarHandle convar,
        std::int32_t slot,
        KeelConVarValue& value) const noexcept = 0;
    virtual KeelResult QueueConVarSet(
        GameConVarHandle convar,
        std::int32_t slot,
        const KeelConVarValue& value) noexcept = 0;
    virtual KeelResult DescribeConVar(
        GameConVarHandle convar,
        KeelConVarInfo& info) const noexcept = 0;
};

std::unique_ptr<GameAdapter> CreateGameAdapter();

}

#endif
