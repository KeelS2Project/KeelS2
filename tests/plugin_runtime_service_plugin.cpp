#include <keels2/plugin.h>
#include <keels2/plugins.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{

const KeelHostApi* g_host{};
const KeelPluginsApi* g_plugins{};
KeelPluginHandle g_plugin{};
KeelCommandHandle g_command{};
std::array<KeelPluginSubscriptionHandle, KEELS2_PLUGIN_EVENT_ALL_LOADED> g_subscriptions{};
std::atomic<bool> g_block_armed{};
std::atomic<bool> g_block_entered{};
std::atomic<bool> g_block_release{};
std::atomic<std::uint32_t> g_callback_count{};
std::atomic<std::uint32_t> g_unload_count{};

void Log(const std::string& message)
{
    if (g_host && g_host->log)
    {
        g_host->log(g_plugin, KEEL_LOG_INFO, message.c_str());
    }
}

const char* EventName(KeelPluginEventType event)
{
    switch (event)
    {
        case KEELS2_PLUGIN_EVENT_LOADED:
            return "loaded";
        case KEELS2_PLUGIN_EVENT_UNLOADED:
            return "unloaded";
        case KEELS2_PLUGIN_EVENT_PAUSED:
            return "paused";
        case KEELS2_PLUGIN_EVENT_RESUMED:
            return "resumed";
        case KEELS2_PLUGIN_EVENT_ALL_LOADED:
            return "all-loaded";
        default:
            return "invalid";
    }
}

void OnPluginEvent(const KeelPluginEvent* event, void*)
{
    if (!event || event->size != sizeof(KeelPluginEvent) ||
        event->plugin.size != sizeof(KeelPluginSnapshot) ||
        event->sequence == 0)
    {
        Log("[Plugin Runtime Test] invalid event envelope");
        return;
    }
    g_callback_count.fetch_add(1, std::memory_order_acq_rel);
    if (event->type == KEELS2_PLUGIN_EVENT_PAUSED &&
        g_block_armed.exchange(false, std::memory_order_acq_rel))
    {
        g_block_entered.store(true, std::memory_order_release);
        g_block_entered.notify_all();
        while (!g_block_release.load(std::memory_order_acquire))
        {
            g_block_release.wait(false, std::memory_order_acquire);
        }
    }
    if (event->type == KEELS2_PLUGIN_EVENT_ALL_LOADED)
    {
        if (event->plugin.handle != 0 || event->plugin.state != KEELS2_PLUGIN_STATE_UNKNOWN)
        {
            Log("[Plugin Runtime Test] invalid all-loaded snapshot");
            return;
        }
        Log("[Plugin Runtime Test] all initial plugins loaded");
        return;
    }
    if (!event->plugin.handle || !event->plugin.name[0])
    {
        Log("[Plugin Runtime Test] invalid plugin snapshot");
        return;
    }
    Log(std::string("[Plugin Runtime Test] ") + EventName(event->type) + " " +
        event->plugin.name);
    if (event->type == KEELS2_PLUGIN_EVENT_LOADED && event->plugin.handle == g_plugin)
    {
        const KeelResult self_pause = g_plugins->pause(g_plugin, g_plugin);
        Log(self_pause == KEEL_RESULT_BUSY
            ? "[Plugin Runtime Test] self-pause safely reported busy"
            : "[Plugin Runtime Test] self-pause did not report busy");
    }
}

void RuntimeProbe(const KeelCommandInvocation*, void*)
{
    std::uint32_t count{};
    if (!g_plugins || g_plugins->count(g_plugin, &count) != KEEL_RESULT_OK || count == 0)
    {
        Log("[Plugin Runtime Test] snapshot count failed");
        return;
    }
    bool saw_self{};
    for (std::uint32_t index{}; index < count; ++index)
    {
        KeelPluginSnapshot snapshot{};
        snapshot.size = sizeof(snapshot);
        if (g_plugins->at(g_plugin, index, &snapshot) != KEEL_RESULT_OK ||
            snapshot.size != sizeof(snapshot) || !snapshot.handle)
        {
            Log("[Plugin Runtime Test] indexed snapshot failed");
            return;
        }
        KeelPluginSnapshot by_handle{};
        by_handle.size = sizeof(by_handle);
        if (g_plugins->get(g_plugin, snapshot.handle, &by_handle) != KEEL_RESULT_OK ||
            std::strcmp(snapshot.name, by_handle.name) != 0)
        {
            Log("[Plugin Runtime Test] handle snapshot failed");
            return;
        }
        saw_self = saw_self || snapshot.handle == g_plugin;
    }
    KeelPluginSnapshot self{};
    self.size = sizeof(self);
    if (!saw_self || g_plugins->find(g_plugin, "Plugin Runtime Observer", &self) != KEEL_RESULT_OK ||
        self.handle != g_plugin || self.state != KEELS2_PLUGIN_STATE_RUNNING)
    {
        Log("[Plugin Runtime Test] named snapshot failed");
        return;
    }
    Log("[Plugin Runtime Test] snapshot APIs passed");
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
        "Plugin Runtime Observer",
        "KeelS2 Tests",
        "0.5D",
        "Exercises plugin snapshots, events, pause, and resume"
    };
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(
    const KeelHostApi* api,
    KeelPluginHandle plugin)
{
    if (!api || api->size != sizeof(KeelHostApi) ||
        api->abi_version != KEELS2_PLUGIN_ABI_VERSION || !api->query_service ||
        !api->register_command || !plugin)
    {
        return KEEL_FALSE;
    }
    g_host = api;
    g_plugin = plugin;
    g_block_armed.store(false, std::memory_order_release);
    g_block_entered.store(false, std::memory_order_release);
    g_block_release.store(false, std::memory_order_release);
    g_callback_count.store(0, std::memory_order_release);
    g_unload_count.store(0, std::memory_order_release);
    const void* raw{};
    if (api->query_service(
            plugin,
            KEELS2_PLUGINS_SERVICE_NAME,
            KEELS2_PLUGINS_API_VERSION,
            &raw) != KEEL_RESULT_OK)
    {
        return KEEL_FALSE;
    }
    g_plugins = static_cast<const KeelPluginsApi*>(raw);
    if (!g_plugins || g_plugins->size != sizeof(KeelPluginsApi) ||
        g_plugins->api_version != KEELS2_PLUGINS_API_VERSION ||
        !g_plugins->count || !g_plugins->at || !g_plugins->get || !g_plugins->find ||
        !g_plugins->pause || !g_plugins->resume || !g_plugins->subscribe ||
        !g_plugins->unsubscribe)
    {
        return KEEL_FALSE;
    }
    if (g_plugins->pause(plugin, plugin) != KEEL_RESULT_NOT_READY ||
        g_plugins->resume(plugin, plugin) != KEEL_RESULT_NOT_READY)
    {
        Log("[Plugin Runtime Test] loading transition was not rejected");
        return KEEL_FALSE;
    }
    Log("[Plugin Runtime Test] loading transition safely rejected");
    for (KeelPluginEventType event = KEELS2_PLUGIN_EVENT_LOADED;
         event <= KEELS2_PLUGIN_EVENT_ALL_LOADED;
         ++event)
    {
        const KeelPluginSubscriptionSpec spec{
            sizeof(KeelPluginSubscriptionSpec),
            event,
            0,
            &OnPluginEvent,
            nullptr
        };
        if (g_plugins->subscribe(plugin, &spec, &g_subscriptions[event - 1]) != KEEL_RESULT_OK ||
            !g_subscriptions[event - 1])
        {
            return KEEL_FALSE;
        }
    }
    const KeelCommandSpec command{
        sizeof(KeelCommandSpec),
        "keel_runtime_probe",
        "Validates the managed plugin runtime service",
        0,
        &RuntimeProbe,
        nullptr
    };
    if (api->register_command(plugin, &command, &g_command) != KEEL_RESULT_OK || !g_command)
    {
        return KEEL_FALSE;
    }
    Log("[Plugin Runtime Test] ready");
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin)
{
    if (plugin == g_plugin)
    {
        g_unload_count.fetch_add(1, std::memory_order_acq_rel);
        Log("[Plugin Runtime Test] unloaded cleanly");
    }
    g_subscriptions = {};
    g_command = 0;
    g_plugins = nullptr;
    g_host = nullptr;
    g_plugin = 0;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_PluginRuntimeArmBlock()
{
    g_block_release.store(false, std::memory_order_release);
    g_block_entered.store(false, std::memory_order_release);
    g_block_armed.store(true, std::memory_order_release);
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_PluginRuntimeBlockEntered()
{
    return g_block_entered.load(std::memory_order_acquire) ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_PluginRuntimeReleaseBlock()
{
    g_block_release.store(true, std::memory_order_release);
    g_block_release.notify_all();
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_PluginRuntimeCallbackCount()
{
    return g_callback_count.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_PluginRuntimeUnloadCount()
{
    return g_unload_count.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_PluginRuntimePauseBasic()
{
    if (!g_plugins || !g_plugin)
    {
        return KEEL_FALSE;
    }
    KeelPluginSnapshot basic{};
    basic.size = sizeof(basic);
    return g_plugins->find(g_plugin, "KeelS2 Basic", &basic) == KEEL_RESULT_OK &&
            basic.handle && g_plugins->pause(g_plugin, basic.handle) == KEEL_RESULT_OK
        ? KEEL_TRUE
        : KEEL_FALSE;
}
