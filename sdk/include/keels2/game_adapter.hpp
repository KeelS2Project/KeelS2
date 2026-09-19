#ifndef KEELS2_GAME_ADAPTER_HPP
#define KEELS2_GAME_ADAPTER_HPP

#include <keels2/bootstrap_api.h>
#include <keels2/convar.h>
#include <keels2/entities.h>
#include <keels2/player_actions.h>
#include <keels2/player_management.h>
#include <keels2/entity_writes.h>
#include <keels2/entity_tools.h>
#include <keels2/entity_keyvalues.h>
#include <keels2/entity_access.h>
#include <keels2/entity_hook_data.h>
#include <keels2/round_control.h>
#include <keels2/player_statistics.h>
#include <keels2/player_input.h>
#include <keels2/players.h>
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

inline constexpr const char* kGameAdapterCommandCallerSymbol = "KeelGameAdapter_CommandCaller";
inline constexpr std::uint64_t kClientCommandFlags = (1ull << 2) | (1ull << 25);
using GameAdapterCommandCallerFn = KeelResult (*)(const void* context, std::int32_t* slot) noexcept;

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
inline constexpr const char* kGameAdapterPlayerActionSymbol = "KeelGameAdapter_PlayerAction";
using GameAdapterPlayerActionFn = KeelResult (*)(GameAdapter*, const GameEntityIdentity*, const KeelPlayerAction*) noexcept;
using GameAdapterDestroyFn = void (*)(GameAdapter* adapter);

inline constexpr const char* kGameAdapterPlayerStatisticsSymbol = "KeelGameAdapter_QueryPlayerStatistics";
inline constexpr std::uint32_t kGameAdapterPlayerStatisticsVersion = 1;
struct GameAdapterPlayerStatisticsApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*capabilities)(GameAdapter*, std::uint32_t*, std::uint32_t*) noexcept;
    KeelResult (*read)(GameAdapter*, const GameEntityIdentity*, std::uint32_t, std::int32_t*) noexcept;
    KeelResult (*write)(GameAdapter*, const GameEntityIdentity*, std::uint32_t, std::int32_t) noexcept;
};
using GameAdapterQueryPlayerStatisticsFn = KeelResult (*)(std::uint32_t, GameAdapterPlayerStatisticsApi*) noexcept;

inline constexpr const char* kGameAdapterRoundControlSymbol = "KeelGameAdapter_QueryRoundControl";
inline constexpr std::uint32_t kGameAdapterRoundControlVersion = 1;
struct GameAdapterRoundControlApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*capabilities)(GameAdapter*, std::uint32_t*) noexcept;
    KeelResult (*terminate)(GameAdapter*, const KeelRoundTermination*) noexcept;
};
using GameAdapterQueryRoundControlFn = KeelResult (*)(std::uint32_t, GameAdapterRoundControlApi*) noexcept;

inline constexpr const char* kGameAdapterEntityHookDataSymbol = "KeelGameAdapter_QueryEntityHookData";
inline constexpr std::uint32_t kGameAdapterEntityHookDataVersion = 1;
struct GameAdapterEntityHookDataApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*read_damage)(GameAdapter*, const void*, KeelDamageInfo*) noexcept;
    KeelResult (*write_damage)(GameAdapter*, void*, const KeelDamageEdit*) noexcept;
    KeelResult (*weapon_matches)(GameAdapter*, const GameEntityIdentity*, const void*, KeelBool*) noexcept;
};
using GameAdapterQueryEntityHookDataFn = KeelResult (*)(std::uint32_t, GameAdapterEntityHookDataApi*) noexcept;

inline constexpr const char* kGameAdapterEntityCaptureSymbol = "KeelGameAdapter_QueryEntityCapture";
inline constexpr std::uint32_t kGameAdapterEntityCaptureVersion = 1;
struct GameAdapterEntityCaptureApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*capture)(GameAdapter*, const void*, GameEntityIdentity*) noexcept;
};
using GameAdapterQueryEntityCaptureFn = KeelResult (*)(std::uint32_t, GameAdapterEntityCaptureApi*) noexcept;

struct GameEntityAccessRequest
{
    GameEntityIdentity entity;
    const char* class_name;
};
inline constexpr const char* kGameAdapterEntityAccessSymbol = "KeelGameAdapter_QueryEntityAccess";
inline constexpr std::uint32_t kGameAdapterEntityAccessVersion = 1;
struct GameAdapterEntityAccessApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*visit)(GameAdapter*, const GameEntityAccessRequest*, std::uint32_t,
        KeelEntityAccessCallback, void*) noexcept;
};
using GameAdapterQueryEntityAccessFn = KeelResult (*)(std::uint32_t, GameAdapterEntityAccessApi*) noexcept;

inline constexpr const char* kGameAdapterEntityToolsSymbol = "KeelGameAdapter_QueryEntityTools";
inline constexpr std::uint32_t kGameAdapterEntityToolsVersion = 1;
struct GameAdapterEntityToolsApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*capabilities)(GameAdapter*, std::uint32_t*) noexcept;
    KeelResult (*apply)(GameAdapter*, const GameEntityIdentity*, std::uint32_t,
        const KeelEntityTeleport*, const char*) noexcept;
};
using GameAdapterQueryEntityToolsFn = KeelResult (*)(std::uint32_t, GameAdapterEntityToolsApi*) noexcept;

inline constexpr const char* kGameAdapterEntityConstructionSymbol = "KeelGameAdapter_QueryEntityConstruction";
inline constexpr std::uint32_t kGameAdapterEntityConstructionVersion = 1;
struct GameAdapterEntityConstructionApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    // Main-thread pending tokens belong to this adapter instance. The host
    // enforces plugin ownership and retains the adapter across game callbacks.
    KeelResult (*ready)(GameAdapter*) noexcept;
    KeelResult (*create)(GameAdapter*, const char*, std::uint64_t*, GameEntityIdentity*) noexcept;
    KeelResult (*describe)(GameAdapter*, std::uint64_t, GameEntityIdentity*) noexcept;
    KeelResult (*set)(GameAdapter*, std::uint64_t, const KeelEntityKeyValue*) noexcept;
    KeelResult (*teleport)(GameAdapter*, std::uint64_t, const KeelEntityTeleport*) noexcept;
    // Invoked consumes the token even on failure; an invoked spawn is never retried.
    KeelResult (*spawn)(GameAdapter*, std::uint64_t, KeelBool*) noexcept;
    KeelResult (*cancel)(GameAdapter*, std::uint64_t) noexcept;
    KeelResult (*visit)(GameAdapter*, std::uint64_t, const char*, KeelEntityAccessCallback, void*) noexcept;
};
using GameAdapterQueryEntityConstructionFn = KeelResult (*)(std::uint32_t, GameAdapterEntityConstructionApi*) noexcept;

inline constexpr const char* kGameAdapterEntityWritesSymbol = "KeelGameAdapter_QueryEntityWrites";
inline constexpr std::uint32_t kGameAdapterEntityWritesVersion = 1;
struct GameAdapterEntityWritesApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*capabilities)(GameAdapter*, std::uint32_t*) noexcept;
    KeelResult (*write)(GameAdapter*, const GameEntityIdentity*, const GameSchemaField*, const void*, std::uint32_t) noexcept;
};
using GameAdapterQueryEntityWritesFn = KeelResult (*)(std::uint32_t, GameAdapterEntityWritesApi*) noexcept;

inline constexpr const char* kGameAdapterPlayerManagementSymbol = "KeelGameAdapter_QueryPlayerManagement";
inline constexpr std::uint32_t kGameAdapterPlayerManagementVersion = 1;
struct GameAdapterPlayerManagementApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*capabilities)(GameAdapter*, std::uint32_t*) noexcept;
    KeelResult (*apply)(GameAdapter*, const GameEntityIdentity*, const KeelPlayerManagementAction*) noexcept;
};
using GameAdapterQueryPlayerManagementFn = KeelResult (*)(std::uint32_t, GameAdapterPlayerManagementApi*) noexcept;

inline constexpr const char* kGameAdapterPlayersSymbol = "KeelGameAdapter_QueryPlayers";
inline constexpr std::uint32_t kGameAdapterPlayersVersion = 1;

struct GameAdapterPlayersApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    std::uint32_t (*capacity)() noexcept;
    KeelResult (*read)(GameAdapter* adapter, std::int32_t slot, KeelPlayerInfo* player) noexcept;
};

using GameAdapterQueryPlayersFn = KeelResult (*)(
    std::uint32_t version, GameAdapterPlayersApi* api) noexcept;

inline constexpr const char* kGameAdapterPlayerInputSymbol = "KeelGameAdapter_QueryPlayerInput";
inline constexpr std::uint32_t kGameAdapterPlayerInputVersion = 1;
struct GameAdapterPlayerInputApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*read)(GameAdapter* adapter, std::int32_t slot, std::uint32_t controller,
        std::uint64_t* buttons, std::uint64_t* context) noexcept;
};
using GameAdapterQueryPlayerInputFn = KeelResult (*)(std::uint32_t version, GameAdapterPlayerInputApi* api) noexcept;

inline constexpr const char* kGameAdapterMessagingSymbol = "KeelGameAdapter_QueryMessaging";
inline constexpr std::uint32_t kGameAdapterMessagingVersion = 1;

struct GameAdapterMessagingApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*chat)(GameAdapter* adapter, std::int32_t slot, KeelBool broadcast, const char* text) noexcept;
};

using GameAdapterQueryMessagingFn = KeelResult (*)(
    std::uint32_t version, GameAdapterMessagingApi* api) noexcept;

inline constexpr const char* kGameAdapterConVarObserversSymbol = "KeelGameAdapter_QueryConVarObservers";
inline constexpr std::uint32_t kGameAdapterConVarObserversVersion = 1;

struct GameAdapterConVarObserversApi
{
    std::uint32_t size;
    std::uint32_t api_version;
    KeelResult (*observe)(GameAdapter* adapter, GameConVarHandle convar,
        GameConVarCallback callback, void* user_data) noexcept;
};

using GameAdapterQueryConVarObserversFn = KeelResult (*)(
    std::uint32_t version, GameAdapterConVarObserversApi* api) noexcept;


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
