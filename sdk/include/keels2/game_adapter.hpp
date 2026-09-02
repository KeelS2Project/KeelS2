#ifndef KEELS2_GAME_ADAPTER_HPP
#define KEELS2_GAME_ADAPTER_HPP

#include <keels2/bootstrap_api.h>
#include <keels2/convar.h>
#include <keels2/entities.h>
#include <keels2/keelhook.h>
#include <keels2/lifecycle.h>
#include <keels2/schema.h>
#include <keels2/source2.h>
#include <keels2/source2_callbacks.h>

#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#if defined(KEELS2_GAME_ADAPTER_BUILD)
#define KEELS2_GAME_ADAPTER_EXPORT __declspec(dllexport)
#else
#define KEELS2_GAME_ADAPTER_EXPORT __declspec(dllimport)
#endif
#else
#define KEELS2_GAME_ADAPTER_EXPORT __attribute__((visibility("default")))
#endif

namespace keels2::host
{

inline constexpr std::uint32_t kGameAdapterAbiVersion = 1;
inline constexpr const char* kGameAdapterQuerySymbol = "KeelGameAdapter_Query";

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
using GameSource2Callback = KeelBool (*)(KeelSource2CallbackEvent& event, void* user_data);
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

struct GameCommandSpec
{
    const char* name;
    const char* description;
    std::uint64_t flags;
    GameCommandCallback callback;
    void* user_data;
};

struct GameSchemaField
{
    void* declaring_class{};
    std::int32_t offset{};
    std::uint32_t value_size{};
    std::uint32_t value_alignment{};
    KeelSchemaModule module{};
    KeelSchemaValueType value_type{};
    std::string class_name;
    std::string field_name;
    std::string module_name;
    std::string compatibility_profile;
};

struct GameEntityIdentity
{
    std::int32_t index{};
    std::uint32_t source2_handle{};
    std::uint64_t epoch{};
};

struct GameInterfaceSnapshot
{
    KeelSource2Capability capability{};
    KeelSource2Factory factory{};
    std::string name;
    std::string module;
    std::string module_path;
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
    virtual bool IsGameThread() const noexcept = 0;
    virtual KeelResult QueryInterface(
        KeelSource2Capability capability,
        KeelSource2InterfaceInfo& info) const noexcept = 0;
    virtual KeelResult QueryNamedInterface(
        KeelSource2Factory factory,
        const char* interface_name,
        KeelSource2InterfaceInfo& info) = 0;
    virtual std::vector<GameInterfaceSnapshot> InterfaceSnapshots() const = 0;
    virtual KeelResult ServerCommand(const char*, std::string&)
    {
        return KEEL_RESULT_UNSUPPORTED;
    }
    virtual KeelResult ClientConsolePrint(std::int32_t, const char*, std::string&)
    {
        return KEEL_RESULT_UNSUPPORTED;
    }
    virtual KeelResult FindUserMessage(const char*, std::uint32_t&, std::string&)
    {
        return KEEL_RESULT_UNSUPPORTED;
    }
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
    virtual KeelResult ListenForGameEvent(const char* name, std::string& error) = 0;
    virtual bool RegisterCommand(
        const GameCommandSpec& spec,
        GameCommandHandle& command,
        std::string& error) = 0;
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
    virtual KeelResult ResolveSchemaField(
        const KeelSchemaFieldSpec& spec,
        GameSchemaField& field,
        std::string& error) = 0;
    virtual KeelResult FindEntityByIndex(
        std::int32_t index,
        GameEntityIdentity& entity,
        std::string& error) = 0;
    virtual KeelResult FindEntityBySource2Handle(
        std::uint32_t source2_handle,
        GameEntityIdentity& entity,
        std::string& error) = 0;
    virtual KeelResult ValidateEntity(
        const GameEntityIdentity& entity,
        std::string& error) = 0;
    virtual KeelResult ReadEntityField(
        const GameEntityIdentity& entity,
        const GameSchemaField& field,
        void* value,
        std::uint32_t value_size,
        std::string& error) = 0;
};

struct GameAdapterHostApi
{
    std::uint32_t size;
    std::uint32_t abi_version;
    std::uint32_t (*begin_command_dispatch)() noexcept;
    void (*end_command_dispatch)() noexcept;
};

using GameAdapterCreateFn = GameAdapter* (*)(const GameAdapterHostApi* host);
using GameAdapterDestroyFn = void (*)(GameAdapter* adapter);

struct GameAdapterProvider
{
    std::uint32_t size;
    std::uint32_t abi_version;
    const char* game;
    const char* platform;
    GameAdapterCreateFn create;
    GameAdapterDestroyFn destroy;
};

using GameAdapterQueryFn = std::uint32_t (*)(
    std::uint32_t abi_version,
    GameAdapterProvider* provider);

}

#endif
