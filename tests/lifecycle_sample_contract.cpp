#include <keels2/lifecycle.h>
#include <keels2/platform/dynamic_library.h>
#include <keels2/plugin.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

std::array<KeelLifecycleSubscriptionSpec, 7> g_specs{};
std::size_t g_subscription_count{};
std::size_t g_unsubscription_count{};
std::vector<std::string> g_logs;

void Log(KeelPluginHandle plugin, KeelLogLevel, const char* message)
{
    if (plugin == 1 && message)
    {
        g_logs.emplace_back(message);
    }
}

bool ContainsLog(const std::string& expected)
{
    for (const auto& log : g_logs)
    {
        if (log == expected)
        {
            return true;
        }
    }
    return false;
}

template <typename Payload>
void Dispatch(std::size_t index, KeelLifecycleEventType type, const Payload& payload)
{
    const KeelLifecycleEvent event{
        sizeof(KeelLifecycleEvent),
        type,
        sizeof(Payload),
        0,
        &payload
    };
    g_specs[index].callback(&event, g_specs[index].user_data);
}

KeelResult RegisterCommand(KeelPluginHandle, const KeelCommandSpec*, KeelCommandHandle*)
{
    return KEEL_RESULT_NOT_READY;
}

KeelResult UnregisterCommand(KeelPluginHandle, KeelCommandHandle)
{
    return KEEL_RESULT_NOT_READY;
}

KeelResult Subscribe(
    KeelPluginHandle plugin,
    const KeelLifecycleSubscriptionSpec* spec,
    KeelLifecycleSubscriptionHandle* subscription)
{
    if (plugin != 1 || !spec || spec->size != sizeof(*spec) || !spec->callback ||
        !subscription || g_subscription_count >= g_specs.size())
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    g_specs[g_subscription_count] = *spec;
    *subscription = ++g_subscription_count;
    return KEEL_RESULT_OK;
}

KeelResult Unsubscribe(KeelPluginHandle plugin, KeelLifecycleSubscriptionHandle subscription)
{
    if (plugin != 1 || subscription == 0 || subscription > g_subscription_count)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    ++g_unsubscription_count;
    return KEEL_RESULT_OK;
}

const KeelLifecycleApi g_lifecycle{
    sizeof(KeelLifecycleApi),
    KEELS2_LIFECYCLE_API_VERSION,
    &Subscribe,
    &Unsubscribe
};

KeelResult QueryService(
    KeelPluginHandle plugin,
    const char* name,
    std::uint32_t version,
    const void** service)
{
    if (!service)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *service = nullptr;
    if (plugin != 1 || !name || std::strcmp(name, KEELS2_LIFECYCLE_SERVICE_NAME) != 0)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (version != KEELS2_LIFECYCLE_API_VERSION)
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    *service = &g_lifecycle;
    return KEEL_RESULT_OK;
}

}

int main(int argument_count, char** arguments)
{
    if (argument_count != 2)
    {
        return 1;
    }
    keels2::platform::DynamicLibrary plugin;
    std::string error;
    if (!plugin.Open(std::filesystem::path(arguments[1]), error))
    {
        return 2;
    }
    const auto query = reinterpret_cast<KeelPluginQueryFn>(plugin.Symbol("KeelPlugin_Query"));
    const auto load = reinterpret_cast<KeelPluginLoadFn>(plugin.Symbol("KeelPlugin_Load"));
    const auto unload = reinterpret_cast<KeelPluginUnloadFn>(plugin.Symbol("KeelPlugin_Unload"));
    if (!query || !load || !unload)
    {
        return 3;
    }
    const KeelHostQuery host_query{
        sizeof(KeelHostQuery), KEELS2_PLUGIN_ABI_VERSION, "test-host", "cs2",
#if defined(_WIN32)
        "win64"
#else
        "linuxsteamrt64"
#endif
    };
    KeelPluginInfo info{};
    info.size = sizeof(KeelPluginInfo);
    if (query(&host_query, &info) != KEEL_TRUE || !info.name ||
        std::strcmp(info.name, "KeelS2 Lifecycle Example") != 0)
    {
        return 4;
    }
    const KeelHostApi api{
        sizeof(KeelHostApi), KEELS2_PLUGIN_ABI_VERSION, &Log,
        &RegisterCommand, &UnregisterCommand, &QueryService
    };
    if (load(&api, 1) != KEEL_TRUE || g_subscription_count != g_specs.size())
    {
        return 5;
    }
    for (std::size_t index = 0; index < g_specs.size(); ++index)
    {
        if (g_specs[index].event != index + 1 || !g_specs[index].callback)
        {
            return 6;
        }
    }
    const KeelLifecycleGameFrame frame{
        sizeof(KeelLifecycleGameFrame), KEEL_TRUE, KEEL_FALSE, KEEL_TRUE
    };
    const KeelLifecycleClientConnected connected{
        sizeof(KeelLifecycleClientConnected), -2, 76561198000000001ULL,
        "Alice", "STEAM_1:1:1", "127.0.0.1:27005", KEEL_FALSE, 0
    };
    const KeelLifecycleClientPutInServer put_in_server{
        sizeof(KeelLifecycleClientPutInServer), 3, 76561198000000002ULL,
        nullptr, 7, 0
    };
    const KeelLifecycleClientActive active{
        sizeof(KeelLifecycleClientActive), 4, 76561198000000003ULL,
        "Bob", KEEL_TRUE, 0
    };
    const KeelLifecycleClientFullyConnected fully_connected{
        sizeof(KeelLifecycleClientFullyConnected), 5
    };
    const KeelLifecycleClientDisconnecting disconnecting{
        sizeof(KeelLifecycleClientDisconnecting), 6, 76561198000000004ULL,
        "Carol", nullptr, 41, 0
    };
    const KeelLifecycleClientSettingsChanged settings_changed{
        sizeof(KeelLifecycleClientSettingsChanged), 7
    };
    Dispatch(0, KEELS2_LIFECYCLE_GAME_FRAME, frame);
    Dispatch(1, KEELS2_LIFECYCLE_CLIENT_CONNECTED, connected);
    Dispatch(2, KEELS2_LIFECYCLE_CLIENT_PUT_IN_SERVER, put_in_server);
    Dispatch(3, KEELS2_LIFECYCLE_CLIENT_ACTIVE, active);
    Dispatch(4, KEELS2_LIFECYCLE_CLIENT_FULLY_CONNECTED, fully_connected);
    Dispatch(5, KEELS2_LIFECYCLE_CLIENT_DISCONNECTING, disconnecting);
    Dispatch(6, KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED, settings_changed);
    if (!ContainsLog("[Lifecycle Example] GameFrame simulating=1 first_tick=0 last_tick=1") ||
        !ContainsLog("[Lifecycle Example] ClientConnected slot=-2 xuid=76561198000000001 name=Alice network_id=STEAM_1:1:1 address=127.0.0.1:27005 fake=0") ||
        !ContainsLog("[Lifecycle Example] ClientPutInServer slot=3 xuid=76561198000000002 name= client_type=7") ||
        !ContainsLog("[Lifecycle Example] ClientActive slot=4 xuid=76561198000000003 name=Bob load_game=1") ||
        !ContainsLog("[Lifecycle Example] ClientFullyConnected slot=5") ||
        !ContainsLog("[Lifecycle Example] ClientDisconnecting slot=6 xuid=76561198000000004 name=Carol network_id= reason=41") ||
        !ContainsLog("[Lifecycle Example] ClientSettingsChanged slot=7"))
    {
        return 7;
    }
    unload(1);
    if (g_unsubscription_count != g_specs.size())
    {
        return 8;
    }
    return 0;
}
