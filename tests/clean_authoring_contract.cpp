#include <keels2/authoring.hpp>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace
{
using namespace keels2::authoring;

KeelResult transportResult = KEEL_RESULT_OK;
KeelResult playerResult = KEEL_RESULT_NOT_FOUND;
std::string delivered;
std::string logText;
unsigned deliveries{};
unsigned registrations{};
unsigned removals{};
bool throwRegistration{};
bool failLoad{};

class Probe final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        .name = "Clean authoring contract",
        .author = "KeelS2 tests",
        .version = "1.1.0",
        .description = "Checked authoring behavior"
    };

    using Plugin::SendToConsole;
    using Plugin::SendToChat;
    using Plugin::SendToChatAll;
    using Plugin::PrintToConsole;
    using Plugin::GetPlayer;
    using Plugin::GetNextPlayer;
    using Plugin::CreateCommand;
    using Plugin::ListenForGameEvent;
    using Plugin::LastResult;
    using Plugin::LastError;

    bool Load() override
    {
        current = this;
        if (failLoad)
        {
            CreateCommand("partial", "partial load", &Probe::Command);
            return false;
        }
        return true;
    }

    void Command(const CCommandContext&, const CCommand&)
    {
    }

    void Event(IGameEvent*)
    {
    }

    static Probe* current;
};

Probe* Probe::current{};
using Adapter = keels2::detail::AuthoringAdapter<Probe>;
static_assert(std::is_same_v<decltype(Probe::current->SendToChat(CPlayerSlot(0), "text")), bool>);
static_assert(std::is_same_v<decltype(Probe::current->PrintToConsole(CPlayerSlot(0), "text")), KeelResult>);
static_assert(static_cast<KeelHookAction>(PLUGIN_CONTINUE) == KH_ACTION_CONTINUE);
static_assert(static_cast<KeelHookAction>(PLUGIN_OVERRIDE) == KH_ACTION_OVERRIDE);
static_assert(static_cast<KeelHookAction>(PLUGIN_SUPERSEDE) == KH_ACTION_SUPERSEDE);

void Log(KeelPluginHandle, KeelLogLevel, const char* text)
{
    logText += text;
}

KeelResult Deliver(KeelPluginHandle, int32, const char* text)
{
    ++deliveries;
    delivered = text;
    return transportResult;
}

KeelResult Broadcast(KeelPluginHandle plugin, const char* text)
{
    return Deliver(plugin, -1, text);
}

KeelResult CheckThread(KeelPluginHandle)
{
    return transportResult;
}

KeelResult GetPlayer(KeelPluginHandle, int32, KeelPlayerInfo*)
{
    return playerResult;
}

KeelResult Validate(KeelPluginHandle, const KeelPlayerConnection*, KeelPlayerInfo*)
{
    return playerResult;
}

KeelResult Register(KeelPluginHandle, const KeelSource2CommandSpec* spec, KeelCommandHandle* handle)
{
    if (throwRegistration)
    {
        throw std::runtime_error("injected registration failure");
    }
    if (std::strcmp(spec->name, "reserved") == 0)
    {
        return KEEL_RESULT_RESERVED_NAME;
    }
    *handle = ++registrations;
    return KEEL_RESULT_OK;
}

KeelResult Remove(KeelPluginHandle, KeelCommandHandle)
{
    ++removals;
    return KEEL_RESULT_OK;
}

KeelResult RegisterLegacy(KeelPluginHandle, const KeelCommandSpec*, KeelCommandHandle*)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult CreateConVar(KeelPluginHandle, const KeelConVarSpec*,
    KeelSource2ConVarChangeCallback, void*, KeelConVarHandle*, void**)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult FindConVar(KeelPluginHandle, const char*, KeelConVarType, KeelConVarHandle*, void**)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult Subscribe(KeelPluginHandle, const KeelSource2SubscriptionSpec*, KeelSource2SubscriptionHandle*)
{
    return KEEL_RESULT_NOT_FOUND;
}

KeelNativeRuntimeApi nativeApi;
KeelPlayersApi playersApi;
KeelSource2AuthoringApi authoringApi;
KeelSource2CallbacksApi callbacksApi;

KeelResult Query(KeelPluginHandle, const char* name, uint32, const void** output)
{
    *output = nullptr;
    if (std::strcmp(name, KEELS2_NATIVE_RUNTIME_SERVICE_NAME) == 0)
    {
        *output = &nativeApi;
    }
    else if (std::strcmp(name, KEELS2_PLAYERS_SERVICE_NAME) == 0)
    {
        *output = &playersApi;
    }
    else if (std::strcmp(name, KEELS2_SOURCE2_AUTHORING_SERVICE_NAME) == 0)
    {
        *output = &authoringApi;
    }
    else if (std::strcmp(name, KEELS2_SOURCE2_CALLBACKS_SERVICE_NAME) == 0)
    {
        *output = &callbacksApi;
    }
    return *output ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
}

int Failure(int code, const char* reason)
{
    std::fprintf(stderr, "clean authoring: %s\n", reason);
    return code;
}
}

int main()
{
    nativeApi.size = sizeof(nativeApi);
    nativeApi.api_version = KEELS2_NATIVE_RUNTIME_API_VERSION;
    nativeApi.check_game_thread = &CheckThread;
    nativeApi.client_console_print = &Deliver;
    nativeApi.client_chat_print = &Deliver;
    nativeApi.broadcast_chat = &Broadcast;
    playersApi.size = sizeof(playersApi);
    playersApi.api_version = KEELS2_PLAYERS_API_VERSION;
    playersApi.get_player = &GetPlayer;
    playersApi.get_next_player = &GetPlayer;
    playersApi.validate_connection = &Validate;
    authoringApi.size = sizeof(authoringApi);
    authoringApi.api_version = KEELS2_SOURCE2_AUTHORING_API_VERSION;
    authoringApi.register_command = &Register;
    authoringApi.unregister_command = &Remove;
    authoringApi.create_convar = &CreateConVar;
    authoringApi.find_convar = &FindConVar;
    authoringApi.release_convar = &Remove;
    callbacksApi.size = sizeof(callbacksApi);
    callbacksApi.api_version = KEELS2_SOURCE2_CALLBACKS_API_VERSION;
    callbacksApi.subscribe = &Subscribe;
    callbacksApi.unsubscribe = &Remove;
    KeelHostApi host{};
    host.size = sizeof(host);
    host.abi_version = KEELS2_PLUGIN_ABI_VERSION;
    host.log = &Log;
    host.register_command = &RegisterLegacy;
    host.unregister_command = &Remove;
    host.query_service = &Query;
    if (!Adapter::Load(&host, 77))
    {
        return Failure(1, "minimal plugin load failed");
    }
    auto& plugin = *Probe::current;
    const CPlayerSlot slot(7);
    if (!plugin.SendToChat(slot, "100% {literal}; quit") || delivered != "100% {literal}; quit" ||
        !plugin.SendToConsole(slot, "{} {} {{literal}}", "100%;quit", 42) ||
        delivered != "100%;quit 42 {literal}" || !plugin.SendToChatAll("hello {}", "everyone") ||
        delivered != "hello everyone" || plugin.LastResult() != KEEL_RESULT_OK || plugin.LastError()[0])
    {
        return Failure(2, "Boolean success or literal/brace formatting changed text");
    }
    const unsigned before = deliveries;
    if (plugin.SendToChat(slot, "{} {}", 42) || plugin.LastResult() != KEEL_RESULT_INVALID_ARGUMENT ||
        !std::strstr(plugin.LastError(), "format") || plugin.SendToChat(slot, "") ||
        plugin.SendToChat(slot, std::string(513, 'x').c_str()) ||
        !std::strstr(plugin.LastError(), "512") ||
        plugin.SendToConsole(slot, std::string(4097, 'x').c_str()) ||
        !std::strstr(plugin.LastError(), "4096") || deliveries != before)
    {
        return Failure(3, "invalid formatting or oversized text reached the transport");
    }
    transportResult = KEEL_RESULT_WRONG_THREAD;
    if (plugin.SendToChat(slot, "text") || plugin.LastResult() != KEEL_RESULT_WRONG_THREAD ||
        !std::strstr(plugin.LastError(), "game thread") ||
        plugin.PrintToConsole(slot, "text") != KEEL_RESULT_WRONG_THREAD)
    {
        return Failure(4, "transport failure lost its diagnostic or old result convention");
    }
    PlayerInfo player;
    player.user_id = 123;
    const std::string previousLog = logText;
    if (plugin.GetPlayer(slot, player) || player.user_id != -1 ||
        plugin.LastResult() != KEEL_RESULT_NOT_FOUND || logText != previousLog ||
        plugin.GetNextPlayer(CPlayerSlot(-1), player) || plugin.LastResult() != KEEL_RESULT_NOT_FOUND)
    {
        return Failure(5, "missing players were logged or confused with success");
    }
    playerResult = KEEL_RESULT_WRONG_THREAD;
    if (plugin.GetPlayer(slot, player) || plugin.LastResult() != KEEL_RESULT_WRONG_THREAD)
    {
        return Failure(6, "player failure was confused with ordinary absence");
    }
    if (plugin.CreateCommand("reserved", "test", &Probe::Command) ||
        plugin.LastResult() != KEEL_RESULT_RESERVED_NAME ||
        logText.find("command 'reserved': name is reserved") == std::string::npos ||
        plugin.ListenForGameEvent("missing_event", &Probe::Event) ||
        logText.find("game event 'missing_event': not found") == std::string::npos)
    {
        return Failure(7, "registration diagnostics omitted the resource or reason");
    }
    throwRegistration = true;
    if (plugin.CreateCommand("throws", "test", &Probe::Command) ||
        plugin.LastResult() != KEEL_RESULT_ENGINE_FAILURE || registrations != 0)
    {
        return Failure(8, "registration exception was not contained");
    }
    throwRegistration = false;
    if (!plugin.CreateCommand("owned", "test", &Probe::Command) ||
        plugin.CreateCommand("owned", "test", &Probe::Command) ||
        plugin.LastResult() != KEEL_RESULT_ALREADY_EXISTS)
    {
        return Failure(9, "command ownership or duplicate rejection failed");
    }
    Adapter::Unload(77);
    if (removals != 1 || plugin.SendToChat(slot, "after unload") ||
        plugin.LastResult() != KEEL_RESULT_NOT_READY)
    {
        return Failure(10, "unload did not retire the command or disable text");
    }
    failLoad = true;
    if (Adapter::Load(&host, 77) || registrations != 2 || removals != 2)
    {
        return Failure(11, "partial load did not retire the command");
    }
    return 0;
}
