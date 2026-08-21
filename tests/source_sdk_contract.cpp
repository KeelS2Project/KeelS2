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
};

static_assert(SOURCE_ENGINE == 25);
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
