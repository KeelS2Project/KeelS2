#include <keels2/lifecycle.h>
#include <keels2/plugin.h>
#include <keels2/source2.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{

const KeelHostApi* g_host{};
const KeelLifecycleApi* g_lifecycle{};
KeelPluginHandle g_plugin{};
std::array<KeelLifecycleSubscriptionHandle, 7> g_subscriptions{};
KeelLifecycleSubscriptionHandle g_order_subscription{};
std::array<bool, 7> g_seen{};
std::array<std::atomic<std::uint32_t>, 7> g_callback_counts{};
bool g_order_seen{};
std::atomic<bool> g_block_armed{};
std::atomic<bool> g_block_entered{};
std::atomic<bool> g_block_release{};
std::atomic<bool> g_loading{};

void Log(const char* message)
{
    if (g_host && g_host->log)
    {
        g_host->log(g_plugin, KEEL_LOG_INFO, message);
    }
}

#if !defined(KEELS2_LIFECYCLE_LIVE)
bool Text(const char* value, const char* expected)
{
    return value && std::strcmp(value, expected) == 0;
}
#endif

void OnLifecycle(const KeelLifecycleEvent* event, void*)
{
    if (!event || event->size != sizeof(KeelLifecycleEvent) || event->reserved != 0 ||
        !event->payload || event->type == 0 || event->type > g_seen.size())
    {
        Log("[Lifecycle Test] invalid event envelope");
        return;
    }
    if (g_loading.load(std::memory_order_acquire))
    {
        Log("[Lifecycle Test] callback entered during load");
        return;
    }
    const std::size_t index = event->type - 1;
    g_callback_counts[index].fetch_add(1, std::memory_order_relaxed);
    if (event->type == KEELS2_LIFECYCLE_GAME_FRAME &&
        g_block_armed.exchange(false, std::memory_order_acq_rel))
    {
        g_block_entered.store(true, std::memory_order_release);
        g_block_entered.notify_all();
        bool released = g_block_release.load(std::memory_order_acquire);
        while (!released)
        {
            g_block_release.wait(released, std::memory_order_acquire);
            released = g_block_release.load(std::memory_order_acquire);
        }
    }
#if defined(KEELS2_LIFECYCLE_LIVE)
    if (event->type == KEELS2_LIFECYCLE_GAME_FRAME &&
        event->payload_size == sizeof(KeelLifecycleGameFrame) && !g_seen[index])
    {
        const auto* payload = static_cast<const KeelLifecycleGameFrame*>(event->payload);
        if (payload->size == sizeof(KeelLifecycleGameFrame))
        {
            g_seen[index] = true;
            Log("[Lifecycle Test] live GameFrame observed");
        }
    }
#else
    bool valid{};
    switch (event->type)
    {
        case KEELS2_LIFECYCLE_GAME_FRAME:
        {
            const auto* payload = static_cast<const KeelLifecycleGameFrame*>(event->payload);
            valid = event->payload_size == sizeof(*payload) && payload->size == sizeof(*payload) &&
                payload->simulating == KEEL_TRUE && payload->first_tick == KEEL_FALSE &&
                payload->last_tick == KEEL_TRUE;
            break;
        }
        case KEELS2_LIFECYCLE_CLIENT_CONNECTED:
        {
            const auto* payload = static_cast<const KeelLifecycleClientConnected*>(event->payload);
            valid = event->payload_size == sizeof(*payload) && payload->size == sizeof(*payload) &&
                payload->slot == 4 && payload->xuid == 76561198000000004ull &&
                Text(payload->name, "Keel") && Text(payload->network_id, "STEAM_1:0:2") &&
                Text(payload->address, "127.0.0.1:27005") && payload->fake_player == KEEL_FALSE &&
                payload->reserved == 0;
            break;
        }
        case KEELS2_LIFECYCLE_CLIENT_PUT_IN_SERVER:
        {
            const auto* payload = static_cast<const KeelLifecycleClientPutInServer*>(event->payload);
            valid = event->payload_size == sizeof(*payload) && payload->size == sizeof(*payload) &&
                payload->slot == 4 && payload->xuid == 76561198000000004ull &&
                Text(payload->name, "Keel") && payload->client_type == 0 && payload->reserved == 0;
            break;
        }
        case KEELS2_LIFECYCLE_CLIENT_ACTIVE:
        {
            const auto* payload = static_cast<const KeelLifecycleClientActive*>(event->payload);
            valid = event->payload_size == sizeof(*payload) && payload->size == sizeof(*payload) &&
                payload->slot == 4 && payload->xuid == 76561198000000004ull &&
                Text(payload->name, "Keel") && payload->load_game == KEEL_FALSE &&
                payload->reserved == 0;
            break;
        }
        case KEELS2_LIFECYCLE_CLIENT_FULLY_CONNECTED:
        {
            const auto* payload = static_cast<const KeelLifecycleClientFullyConnected*>(event->payload);
            valid = event->payload_size == sizeof(*payload) && payload->size == sizeof(*payload) &&
                payload->slot == 4;
            break;
        }
        case KEELS2_LIFECYCLE_CLIENT_DISCONNECTING:
        {
            const auto* payload = static_cast<const KeelLifecycleClientDisconnecting*>(event->payload);
            valid = event->payload_size == sizeof(*payload) && payload->size == sizeof(*payload) &&
                payload->slot == 4 && payload->xuid == 76561198000000004ull &&
                Text(payload->name, "Keel") && Text(payload->network_id, "STEAM_1:0:2") &&
                payload->reason == 39 && payload->reserved == 0;
            break;
        }
        case KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED:
        {
            const auto* payload = static_cast<const KeelLifecycleClientSettingsChanged*>(event->payload);
            valid = event->payload_size == sizeof(*payload) && payload->size == sizeof(*payload) &&
                payload->slot == 4;
            break;
        }
        default:
            break;
    }
    if (!valid)
    {
        Log("[Lifecycle Test] invalid event payload");
        return;
    }
    if (!g_seen[index])
    {
        g_seen[index] = true;
        static constexpr std::array messages{
            "[Lifecycle Test] GameFrame",
            "[Lifecycle Test] ClientConnected",
            "[Lifecycle Test] ClientPutInServer",
            "[Lifecycle Test] ClientActive",
            "[Lifecycle Test] ClientFullyConnected",
            "[Lifecycle Test] ClientDisconnecting",
            "[Lifecycle Test] ClientSettingsChanged"
        };
        Log(messages[index]);
    }
#endif
}

#if !defined(KEELS2_LIFECYCLE_LIVE)
void OnSecondGameFrame(const KeelLifecycleEvent* event, void*)
{
    if (!event || event->type != KEELS2_LIFECYCLE_GAME_FRAME || !g_seen[0])
    {
        Log("[Lifecycle Test] registration order failed");
        return;
    }
    if (!g_order_seen)
    {
        g_order_seen = true;
        Log("[Lifecycle Test] registration order passed");
        if (!g_lifecycle ||
            g_lifecycle->unsubscribe(g_plugin, g_order_subscription) != KEEL_RESULT_BUSY)
        {
            Log("[Lifecycle Test] self unsubscribe failed");
        }
        else
        {
            Log("[Lifecycle Test] self unsubscribe safely deferred");
        }
    }
}
#endif

void OnRemovedSubscription(const KeelLifecycleEvent*, void*)
{
    Log("[Lifecycle Test] removed subscription dispatched");
}

}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(
    const KeelHostQuery* query,
    KeelPluginInfo* info)
{
    if (!query || query->size != sizeof(KeelHostQuery) ||
        query->abi_version != KEELS2_PLUGIN_ABI_VERSION || !info ||
        info->size != sizeof(KeelPluginInfo))
    {
        return KEEL_FALSE;
    }
    *info = {
        sizeof(KeelPluginInfo),
        KEELS2_PLUGIN_ABI_VERSION,
        "Lifecycle Test",
        "KeelS2",
        "0.5B",
        "Validates the host-owned Source 2 lifecycle bridge"
    };
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(
    const KeelHostApi* api,
    KeelPluginHandle plugin)
{
    if (!api || api->size != sizeof(KeelHostApi) ||
        api->abi_version != KEELS2_PLUGIN_ABI_VERSION || !api->query_service)
    {
        return KEEL_FALSE;
    }
    g_host = api;
    g_plugin = plugin;
    g_lifecycle = nullptr;
    g_subscriptions = {};
    g_order_subscription = 0;
    g_seen = {};
    for (auto& count : g_callback_counts)
    {
        count.store(0, std::memory_order_relaxed);
    }
    g_order_seen = false;
    g_block_armed.store(false, std::memory_order_release);
    g_block_entered.store(false, std::memory_order_release);
    g_block_release.store(false, std::memory_order_release);
    g_loading.store(true, std::memory_order_release);

    const void* raw{};
    if (api->query_service(
            plugin,
            KEELS2_LIFECYCLE_SERVICE_NAME,
            KEELS2_LIFECYCLE_API_VERSION + 1,
            &raw) != KEEL_RESULT_INCOMPATIBLE || raw)
    {
        return KEEL_FALSE;
    }
    if (api->query_service(
            plugin,
            KEELS2_LIFECYCLE_SERVICE_NAME,
            KEELS2_LIFECYCLE_API_VERSION,
            &raw) != KEEL_RESULT_OK || !raw)
    {
        return KEEL_FALSE;
    }
    const auto* lifecycle = static_cast<const KeelLifecycleApi*>(raw);
    if (lifecycle->size != sizeof(KeelLifecycleApi) ||
        lifecycle->api_version != KEELS2_LIFECYCLE_API_VERSION ||
        !lifecycle->subscribe || !lifecycle->unsubscribe)
    {
        return KEEL_FALSE;
    }
    g_lifecycle = lifecycle;

    KeelLifecycleSubscriptionSpec invalid{
        0,
        KEELS2_LIFECYCLE_GAME_FRAME,
        0,
        &OnLifecycle,
        nullptr
    };
    KeelLifecycleSubscriptionHandle temporary{};
    if (lifecycle->subscribe(plugin, &invalid, &temporary) != KEEL_RESULT_INVALID_ARGUMENT || temporary)
    {
        return KEEL_FALSE;
    }
    invalid.size = sizeof(invalid);
    invalid.event = KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED + 1;
    if (lifecycle->subscribe(plugin, &invalid, &temporary) != KEEL_RESULT_INVALID_ARGUMENT || temporary)
    {
        return KEEL_FALSE;
    }

    const KeelLifecycleSubscriptionSpec temporary_spec{
        sizeof(KeelLifecycleSubscriptionSpec),
        KEELS2_LIFECYCLE_GAME_FRAME,
        0,
        &OnRemovedSubscription,
        nullptr
    };
    if (lifecycle->subscribe(plugin, &temporary_spec, &temporary) != KEEL_RESULT_OK || !temporary ||
        lifecycle->unsubscribe(plugin + 1, temporary) != KEEL_RESULT_NOT_FOUND ||
        lifecycle->unsubscribe(plugin, temporary) != KEEL_RESULT_OK ||
        lifecycle->unsubscribe(plugin, temporary) != KEEL_RESULT_NOT_FOUND)
    {
        return KEEL_FALSE;
    }

#if defined(KEELS2_LIFECYCLE_LIVE)
    constexpr std::uint32_t event_count = 1;
#else
    constexpr std::uint32_t event_count = 7;
#endif
    for (std::uint32_t index{}; index < event_count; ++index)
    {
        const KeelLifecycleSubscriptionSpec spec{
            sizeof(KeelLifecycleSubscriptionSpec),
            index + 1,
            0,
            &OnLifecycle,
            nullptr
        };
        if (lifecycle->subscribe(plugin, &spec, &g_subscriptions[index]) != KEEL_RESULT_OK ||
            !g_subscriptions[index])
        {
            return KEEL_FALSE;
        }
    }
#if !defined(KEELS2_LIFECYCLE_LIVE)
    const KeelLifecycleSubscriptionSpec order_spec{
        sizeof(KeelLifecycleSubscriptionSpec),
        KEELS2_LIFECYCLE_GAME_FRAME,
        0,
        &OnSecondGameFrame,
        nullptr
    };
    if (lifecycle->subscribe(plugin, &order_spec, &g_order_subscription) != KEEL_RESULT_OK ||
        !g_order_subscription)
    {
        return KEEL_FALSE;
    }
#endif
#if !defined(KEELS2_LIFECYCLE_LIVE)
    const void* source2_raw{};
    if (api->query_service(
            plugin,
            KEELS2_SOURCE2_SERVICE_NAME,
            KEELS2_SOURCE2_API_VERSION,
            &source2_raw) != KEEL_RESULT_OK || !source2_raw)
    {
        return KEEL_FALSE;
    }
    const auto* source2 = static_cast<const KeelSource2Api*>(source2_raw);
    KeelSource2InterfaceInfo server{};
    server.size = sizeof(server);
    if (!source2->query_interface ||
        source2->query_interface(plugin, KEELS2_SOURCE2_CAPABILITY_SERVER, &server) != KEEL_RESULT_OK ||
        !server.instance)
    {
        return KEEL_FALSE;
    }
    void* address = (*static_cast<void***>(server.instance))[19];
    using GameFrameFunction = void (*)(void*, bool, bool, bool);
    GameFrameFunction game_frame{};
    static_assert(sizeof(game_frame) == sizeof(address));
    std::memcpy(&game_frame, &address, sizeof(game_frame));
    game_frame(server.instance, true, false, true);
#endif
    g_loading.store(false, std::memory_order_release);
    Log("[Lifecycle Test] loaded");
    const char* fail_load = std::getenv("KEELS2_TEST_LIFECYCLE_FAIL_LOAD");
    if (fail_load && std::strcmp(fail_load, "1") == 0)
    {
        Log("[Lifecycle Test] rejecting load after staged subscriptions");
        return KEEL_FALSE;
    }
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle)
{
    Log("[Lifecycle Test] unloaded");
    g_subscriptions = {};
    g_order_subscription = 0;
    g_seen = {};
    g_order_seen = false;
    g_loading.store(false, std::memory_order_release);
    g_plugin = 0;
    g_lifecycle = nullptr;
    g_host = nullptr;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_LifecycleArmBlock()
{
    g_block_release.store(false, std::memory_order_release);
    g_block_entered.store(false, std::memory_order_release);
    g_block_armed.store(true, std::memory_order_release);
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_LifecycleBlockEntered()
{
    return g_block_entered.load(std::memory_order_acquire) ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_LifecycleReleaseBlock()
{
    g_block_release.store(true, std::memory_order_release);
    g_block_release.notify_all();
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_LifecycleCallbackCount(
    std::uint32_t event)
{
    return event > 0 && event <= g_callback_counts.size()
        ? g_callback_counts[event - 1].load(std::memory_order_relaxed)
        : 0;
}
