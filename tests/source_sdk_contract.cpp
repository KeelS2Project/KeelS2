#include <keels2/keels2.hpp>
#include <keels2/source2_sdk.hpp>

#include <cstdint>
#include <type_traits>
#include <utility>

using ClientConnectSignature = bool (IServerGameClients::*)(
    CPlayerSlot,
    const char*,
    uint64,
    const char*,
    bool,
    CBufferString*);
using ClientActiveSignature = void (IServerGameClients::*)(
    CPlayerSlot,
    bool,
    const char*,
    uint64);
using ClientDisconnectSignature = void (IServerGameClients::*)(
    CPlayerSlot,
    ENetworkDisconnectionReason,
    const char*,
    uint64,
    const char*);
using ClientCommandSignature = void (IServerGameClients::*)(CPlayerSlot, const CCommand&);
using GameFrameSignature = void (IServerGameDLL::*)(bool, bool, bool);
using PluginClientConnectedSignature = void (keels2::Plugin::*)(
    CPlayerSlot,
    const char*,
    uint64,
    const char*,
    const char*,
    bool);
using PluginClientPutInServerSignature = void (keels2::Plugin::*)(
    CPlayerSlot,
    const char*,
    int,
    uint64);
using PluginClientActiveSignature = void (keels2::Plugin::*)(
    CPlayerSlot,
    bool,
    const char*,
    uint64);
using PluginClientFullyConnectedSignature = void (keels2::Plugin::*)(CPlayerSlot);
using PluginClientDisconnectingSignature = void (keels2::Plugin::*)(
    CPlayerSlot,
    ENetworkDisconnectionReason,
    const char*,
    uint64,
    const char*);
using PluginClientSettingsChangedSignature = void (keels2::Plugin::*)(CPlayerSlot);

class Source2AccessProbe : public keels2::Plugin
{
public:
    using keels2::Plugin::GetEngineInterface;
    using keels2::Plugin::GetServerInterface;

    bool RegisterGameFrameHook(IServerGameDLL* server)
    {
        return HookPre(
            server,
            &IServerGameDLL::GameFrame,
            &Source2AccessProbe::OnGameFrameHook);
    }

    PluginResult OnGameFrameHook(
        bool,
        bool,
        bool)
    {
        return plugin_continue;
    }

    void OnGameFrameObserver(bool, bool, bool)
    {
    }

    void OnGameFrameWrong(int, bool, bool)
    {
    }
};

static_assert(SOURCE_ENGINE == 25);
static_assert(plugin_continue == KH_ACTION_CONTINUE);
static_assert(plugin_override == KH_ACTION_OVERRIDE);
static_assert(plugin_supersede == KH_ACTION_SUPERSEDE);
static_assert(sizeof(void*) == 8);
static_assert(sizeof(uint64) == 8);
static_assert(sizeof(CPlayerSlot) == 4);
static_assert(sizeof(CCommandContext) == 8);
static_assert(sizeof(ENetworkDisconnectionReason) == 4);
static_assert(sizeof(NETWORKSERVERSERVICE_INTERFACE_VERSION) == 25);
static_assert(std::is_same_v<decltype(&IServerGameClients::ClientConnect), ClientConnectSignature>);
static_assert(std::is_same_v<decltype(&IServerGameClients::ClientActive), ClientActiveSignature>);
static_assert(std::is_same_v<
    decltype(&IServerGameClients::ClientDisconnect),
    ClientDisconnectSignature>);
static_assert(std::is_same_v<decltype(&IServerGameClients::ClientCommand), ClientCommandSignature>);
static_assert(std::is_same_v<decltype(&IServerGameDLL::GameFrame), GameFrameSignature>);
static_assert(std::is_same_v<
    keels2::kh::MethodClass<&IServerGameDLL::GameFrame>,
    IServerGameDLL>);
static_assert(std::is_same_v<
    keels2::kh::MethodSignature<&IServerGameDLL::GameFrame>,
    void(bool, bool, bool)>);
static_assert(keels2::kh::CompatibleMethodCallback<
    &IServerGameDLL::GameFrame,
    &Source2AccessProbe::OnGameFrameHook>);
static_assert(keels2::kh::CompatibleMethodCallback<
    &IServerGameDLL::GameFrame,
    &Source2AccessProbe::OnGameFrameObserver>);
static_assert(!keels2::kh::CompatibleMethodCallback<
    &IServerGameDLL::GameFrame,
    &Source2AccessProbe::OnGameFrameWrong>);
static_assert(keels2::kh::ValueTypeV<CPlayerSlot> == KH_VALUE_INT32);
static_assert(keels2::kh::ValueTypeV<CSplitScreenSlot> == KH_VALUE_INT32);
static_assert(keels2::kh::ValueTypeV<ENetworkDisconnectionReason> == KH_VALUE_INT32);
static_assert(keels2::kh::ValueTypeV<const CCommand&> == KH_VALUE_POINTER);
static_assert(keels2::kh::ValueTypeV<int64> == KH_VALUE_INT64);
static_assert(keels2::kh::ValueTypeV<uint64> == KH_VALUE_UINT64);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameDLL::GameFrame>>::value.argument_count == 4);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameDLL::GameFrame>>::arguments[0] ==
    KH_VALUE_POINTER);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameDLL::GameFrame>>::arguments[1] ==
    KH_VALUE_BOOL);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameDLL::GameFrame>>::arguments[2] ==
    KH_VALUE_BOOL);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameDLL::GameFrame>>::arguments[3] ==
    KH_VALUE_BOOL);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameClients::ClientCommand>>::value.argument_count ==
    3);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameClients::ClientCommand>>::arguments[1] ==
    KH_VALUE_INT32);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameClients::ClientCommand>>::arguments[2] ==
    KH_VALUE_POINTER);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameClients::ClientDisconnect>>::arguments[1] ==
    KH_VALUE_INT32);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameClients::ClientDisconnect>>::arguments[2] ==
    KH_VALUE_INT32);
static_assert(
    keels2::kh::MethodPrototype<
        keels2::kh::MethodSignature<&IServerGameClients::ClientDisconnect>>::arguments[4] ==
    KH_VALUE_UINT64);
static_assert(std::is_same_v<
    decltype(std::declval<Source2AccessProbe&>().GetEngineInterface<INetworkServerService>(
        NETWORKSERVERSERVICE_INTERFACE_VERSION)),
    INetworkServerService*>);
static_assert(std::is_same_v<
    decltype(std::declval<Source2AccessProbe&>().GetServerInterface<IServerGameDLL>(
        INTERFACEVERSION_SERVERGAMEDLL)),
    IServerGameDLL*>);
static_assert(std::is_same_v<
    decltype(&keels2::Plugin::OnClientConnected),
    PluginClientConnectedSignature>);
static_assert(std::is_same_v<
    decltype(&keels2::Plugin::OnClientPutInServer),
    PluginClientPutInServerSignature>);
static_assert(std::is_same_v<
    decltype(&keels2::Plugin::OnClientActive),
    PluginClientActiveSignature>);
static_assert(std::is_same_v<
    decltype(&keels2::Plugin::OnClientFullyConnected),
    PluginClientFullyConnectedSignature>);
static_assert(std::is_same_v<
    decltype(&keels2::Plugin::OnClientDisconnecting),
    PluginClientDisconnectingSignature>);
static_assert(std::is_same_v<
    decltype(&keels2::Plugin::OnClientSettingsChanged),
    PluginClientSettingsChangedSignature>);
static_assert(std::is_same_v<decltype(KeelLifecycleClientActive::slot), std::int32_t>);
static_assert(std::is_same_v<decltype(KeelLifecycleClientActive::xuid), std::uint64_t>);
static_assert(std::is_same_v<
    decltype(KeelLifecycleClientDisconnecting::reason),
    std::int32_t>);
static_assert(std::is_class_v<CConVar<int>>);
static_assert(std::is_class_v<CConVarRef<int>>);
static_assert(std::is_class_v<ConVarRefAbstract>);
