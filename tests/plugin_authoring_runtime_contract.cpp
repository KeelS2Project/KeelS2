#include <keels2/platform/dynamic_library.h>
#include <keels2/plugin.h>
#include <keels2/plugins.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{

struct Subscription
{
    KeelPluginSubscriptionSpec spec{};
    KeelPluginSubscriptionHandle handle{};
    KeelPluginHandle owner{};
    bool active{};
};

std::vector<Subscription> g_subscriptions;
std::vector<std::pair<KeelLogLevel, std::string>> g_logs;
KeelPluginSubscriptionHandle g_next_subscription{1};
KeelPluginEventType g_fail_subscription{};
bool g_service_available{true};
std::uint32_t g_service_queries{};
std::uint32_t g_unsubscribe_count{};
std::uint32_t g_pause_count{};
std::uint32_t g_resume_count{};

KeelPluginSnapshot Snapshot(
    KeelPluginHandle handle,
    KeelPluginRuntimeState state,
    const char* name,
    const char* version,
    const char* file)
{
    KeelPluginSnapshot snapshot{};
    snapshot.size = sizeof(snapshot);
    snapshot.state = state;
    snapshot.handle = handle;
    std::snprintf(snapshot.name, sizeof(snapshot.name), "%s", name);
    std::snprintf(snapshot.author, sizeof(snapshot.author), "%s", "KeelS2 Tests");
    std::snprintf(snapshot.version, sizeof(snapshot.version), "%s", version);
    std::snprintf(snapshot.description, sizeof(snapshot.description), "%s",
        handle == 22 ? "Runtime target" : "Runtime observer");
    std::snprintf(snapshot.file, sizeof(snapshot.file), "%s", file);
    return snapshot;
}

KeelPluginSnapshot AllLoadedSnapshot()
{
    KeelPluginSnapshot snapshot{};
    snapshot.size = sizeof(snapshot);
    snapshot.state = KEELS2_PLUGIN_STATE_UNKNOWN;
    return snapshot;
}

KeelPluginSnapshot g_snapshots[]{
    Snapshot(11, KEELS2_PLUGIN_STATE_RUNNING, "Observer Plugin", "1.0.0", "observer.so"),
    Snapshot(22, KEELS2_PLUGIN_STATE_RUNNING, "Target Plugin", "3.4.5", "target.so")
};

void ResetHost()
{
    g_subscriptions.clear();
    g_logs.clear();
    g_next_subscription = 1;
    g_fail_subscription = 0;
    g_service_available = true;
    g_service_queries = 0;
    g_unsubscribe_count = 0;
    g_pause_count = 0;
    g_resume_count = 0;
}

int Failure(int code, const char* message)
{
    std::fprintf(stderr, "plugin authoring runtime contract failure: %s\n", message);
    return code;
}

bool HasLog(KeelLogLevel level, const char* text)
{
    return std::any_of(g_logs.begin(), g_logs.end(), [&](const auto& entry) {
        return entry.first == level && entry.second.find(text) != std::string::npos;
    });
}

void Log(KeelPluginHandle, KeelLogLevel level, const char* message)
{
    if (message)
    {
        g_logs.emplace_back(level, message);
    }
}

KeelResult RegisterCommand(KeelPluginHandle, const KeelCommandSpec*, KeelCommandHandle*)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult UnregisterCommand(KeelPluginHandle, KeelCommandHandle)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult Count(KeelPluginHandle plugin, std::uint32_t* count)
{
    if (plugin != 77 || !count)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *count = 2;
    return KEEL_RESULT_OK;
}

KeelResult At(
    KeelPluginHandle plugin,
    std::uint32_t index,
    KeelPluginSnapshot* snapshot)
{
    if (plugin != 77 || !snapshot || snapshot->size != sizeof(KeelPluginSnapshot))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (index >= 2)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    *snapshot = g_snapshots[index];
    return KEEL_RESULT_OK;
}

KeelResult Get(
    KeelPluginHandle plugin,
    KeelPluginHandle target,
    KeelPluginSnapshot* snapshot)
{
    if (plugin != 77 || !snapshot || snapshot->size != sizeof(KeelPluginSnapshot))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    for (const auto& candidate : g_snapshots)
    {
        if (candidate.handle == target)
        {
            *snapshot = candidate;
            return KEEL_RESULT_OK;
        }
    }
    return KEEL_RESULT_NOT_FOUND;
}

KeelResult Find(
    KeelPluginHandle plugin,
    const char* name,
    KeelPluginSnapshot* snapshot)
{
    if (plugin != 77 || !name || !snapshot || snapshot->size != sizeof(KeelPluginSnapshot))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    for (const auto& candidate : g_snapshots)
    {
        if (std::strcmp(candidate.name, name) == 0)
        {
            *snapshot = candidate;
            return KEEL_RESULT_OK;
        }
    }
    return KEEL_RESULT_NOT_FOUND;
}

KeelResult Pause(KeelPluginHandle plugin, KeelPluginHandle target)
{
    if (plugin != 77 || target != 22)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    ++g_pause_count;
    return KEEL_RESULT_OK;
}

KeelResult Resume(KeelPluginHandle plugin, KeelPluginHandle target)
{
    if (plugin != 77 || target != 22)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    ++g_resume_count;
    return KEEL_RESULT_OK;
}

KeelResult Subscribe(
    KeelPluginHandle plugin,
    const KeelPluginSubscriptionSpec* spec,
    KeelPluginSubscriptionHandle* output)
{
    if (plugin != 77 || !spec || spec->size != sizeof(KeelPluginSubscriptionSpec) ||
        spec->reserved != 0 || !spec->callback || !output)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (spec->event == g_fail_subscription)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    Subscription record;
    record.spec = *spec;
    record.handle = g_next_subscription++;
    record.owner = plugin;
    record.active = true;
    *output = record.handle;
    g_subscriptions.push_back(record);

    KeelPluginEvent staged{};
    staged.size = sizeof(staged);
    staged.type = spec->event;
    staged.sequence = 1;
    staged.plugin = spec->event == KEELS2_PLUGIN_EVENT_ALL_LOADED
        ? AllLoadedSnapshot()
        : g_snapshots[1];
    spec->callback(&staged, spec->user_data);
    return KEEL_RESULT_OK;
}

KeelResult Unsubscribe(KeelPluginHandle plugin, KeelPluginSubscriptionHandle handle)
{
    for (auto& record : g_subscriptions)
    {
        if (record.owner == plugin && record.handle == handle && record.active)
        {
            record.active = false;
            ++g_unsubscribe_count;
            return KEEL_RESULT_OK;
        }
    }
    return KEEL_RESULT_NOT_FOUND;
}

const KeelPluginsApi g_plugins{
    sizeof(KeelPluginsApi),
    KEELS2_PLUGINS_API_VERSION,
    &Count,
    &At,
    &Get,
    &Find,
    &Pause,
    &Resume,
    &Subscribe,
    &Unsubscribe
};

KeelResult QueryService(
    KeelPluginHandle plugin,
    const char* name,
    std::uint32_t version,
    const void** service)
{
    ++g_service_queries;
    if (service)
    {
        *service = nullptr;
    }
    if (plugin != 77 || !name || !service)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (!g_service_available || std::strcmp(name, KEELS2_PLUGINS_SERVICE_NAME) != 0 ||
        version != KEELS2_PLUGINS_API_VERSION)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    *service = &g_plugins;
    return KEEL_RESULT_OK;
}

void Dispatch(KeelPluginEventType event, KeelPluginRuntimeState state)
{
    for (const auto& record : g_subscriptions)
    {
        if (!record.active || record.spec.event != event)
        {
            continue;
        }
        KeelPluginEvent envelope{};
        envelope.size = sizeof(envelope);
        envelope.type = event;
        envelope.sequence = 100 + event;
        if (event == KEELS2_PLUGIN_EVENT_ALL_LOADED)
        {
            envelope.plugin.size = sizeof(KeelPluginSnapshot);
            envelope.plugin.state = KEELS2_PLUGIN_STATE_UNKNOWN;
        }
        else
        {
            envelope.plugin = g_snapshots[1];
            envelope.plugin.state = state;
        }
        record.spec.callback(&envelope, record.spec.user_data);
    }
}

bool NoActiveSubscriptions()
{
    return std::none_of(g_subscriptions.begin(), g_subscriptions.end(), [](const auto& record) {
        return record.active;
    });
}

}

int main(int argument_count, char** arguments)
{
    if (argument_count != 2)
    {
        return Failure(1, "expected one plugin path");
    }
    keels2::platform::DynamicLibrary library;
    std::string error;
    if (!library.Open(arguments[1], error))
    {
        return Failure(2, error.c_str());
    }

    const auto query = reinterpret_cast<KeelPluginQueryFn>(library.Symbol("KeelPlugin_Query"));
    const auto manifest = reinterpret_cast<KeelPluginManifestFn>(library.Symbol("KeelPlugin_Manifest"));
    const auto load = reinterpret_cast<KeelPluginLoadFn>(library.Symbol("KeelPlugin_Load"));
    const auto unload = reinterpret_cast<KeelPluginUnloadFn>(library.Symbol("KeelPlugin_Unload"));
    using ResetFunction = void (*)();
    using ModeFunction = void (*)(std::uint32_t);
    using ValueFunction = std::uint32_t (*)(std::uint32_t);
    const auto reset = reinterpret_cast<ResetFunction>(
        library.Symbol("KeelTest_PluginAuthoringRuntimeReset"));
    const auto mode = reinterpret_cast<ModeFunction>(
        library.Symbol("KeelTest_PluginAuthoringRuntimeMode"));
    const auto value = reinterpret_cast<ValueFunction>(
        library.Symbol("KeelTest_PluginAuthoringRuntimeValue"));
    if (!query || !manifest || !load || !unload || !reset || !mode || !value)
    {
        return Failure(3, "required exports are missing");
    }

    const KeelHostQuery host_query{
        sizeof(KeelHostQuery), KEELS2_PLUGIN_ABI_VERSION, "0.5D", "cs2", "test"
    };
    KeelPluginInfo info{};
    info.size = sizeof(info);
    if (query(&host_query, &info) != KEEL_TRUE || !info.name ||
        std::strcmp(info.name, "Plugin Authoring Runtime") != 0)
    {
        return Failure(4, "metadata query failed");
    }

    KeelPluginManifest dependency_manifest{};
    dependency_manifest.size = sizeof(dependency_manifest);
    if (manifest(&host_query, &dependency_manifest) != KEEL_TRUE ||
        dependency_manifest.manifest_version != KEELS2_PLUGIN_MANIFEST_VERSION ||
        dependency_manifest.dependency_count != 2 || !dependency_manifest.dependencies ||
        std::strcmp(dependency_manifest.dependencies[0].name, "Core Plugin") != 0 ||
        std::strcmp(dependency_manifest.dependencies[0].version, "1.2.3") != 0 ||
        dependency_manifest.dependencies[0].requirement != KEELS2_PLUGIN_DEPENDENCY_EXACT ||
        std::strcmp(dependency_manifest.dependencies[1].name, "Utility Plugin") != 0 ||
        std::strcmp(dependency_manifest.dependencies[1].version, "2.0.0") != 0 ||
        dependency_manifest.dependencies[1].requirement != KEELS2_PLUGIN_DEPENDENCY_AT_LEAST)
    {
        return Failure(5, "dependency manifest mapping failed");
    }
    mode(1);
    dependency_manifest = {};
    dependency_manifest.size = sizeof(dependency_manifest);
    if (manifest(&host_query, &dependency_manifest) != KEEL_FALSE)
    {
        return Failure(6, "dependency exception escaped the manifest boundary");
    }
    mode(0);

    const KeelHostApi api{
        sizeof(KeelHostApi),
        KEELS2_PLUGIN_ABI_VERSION,
        &Log,
        &RegisterCommand,
        &UnregisterCommand,
        &QueryService
    };
    ResetHost();
    reset();
    if (load(&api, 77) != KEEL_TRUE || g_service_queries != 1 ||
        g_subscriptions.size() != 5 || value(0) != 1 || value(8) != 1 ||
        g_pause_count != 0 || g_resume_count != 0)
    {
        return Failure(7, "friendly runtime setup or helper calls failed");
    }
    for (std::size_t index{}; index < g_subscriptions.size(); ++index)
    {
        if (g_subscriptions[index].spec.event != index + 1)
        {
            return Failure(8, "runtime events were not selectively subscribed in order");
        }
    }
    if (value(2) || value(3) || value(4) || value(5) || value(6))
    {
        return Failure(9, "plugin event entered author code during load");
    }

    Dispatch(KEELS2_PLUGIN_EVENT_LOADED, KEELS2_PLUGIN_STATE_RUNNING);
    Dispatch(KEELS2_PLUGIN_EVENT_UNLOADED, KEELS2_PLUGIN_STATE_UNKNOWN);
    Dispatch(KEELS2_PLUGIN_EVENT_PAUSED, KEELS2_PLUGIN_STATE_PAUSED);
    Dispatch(KEELS2_PLUGIN_EVENT_RESUMED, KEELS2_PLUGIN_STATE_RUNNING);
    Dispatch(KEELS2_PLUGIN_EVENT_ALL_LOADED, KEELS2_PLUGIN_STATE_UNKNOWN);
    if (value(2) != 1 || value(3) != 1 || value(4) != 1 || value(5) != 1 ||
        value(6) != 1 || value(7) != 1 || value(8) != 1 ||
        g_pause_count != 1 || g_resume_count != 1 ||
        !HasLog(KEEL_LOG_ERROR, "exception escaped a plugin event callback"))
    {
        return Failure(10, "plugin event mapping or exception containment failed");
    }

    const auto prior_loaded = value(2);
    KeelPluginEvent malformed{};
    malformed.type = KEELS2_PLUGIN_EVENT_LOADED;
    malformed.sequence = 900;
    malformed.plugin = g_snapshots[1];
    for (const auto& record : g_subscriptions)
    {
        if (record.spec.event == KEELS2_PLUGIN_EVENT_LOADED)
        {
            record.spec.callback(&malformed, record.spec.user_data);
        }
    }
    if (value(2) != prior_loaded)
    {
        return Failure(11, "malformed plugin event reached author code");
    }

    const auto saved = g_subscriptions;
    unload(77);
    if (value(1) != 1 || value(9) != 1 || g_unsubscribe_count != 5 ||
        !NoActiveSubscriptions())
    {
        return Failure(12, "successful unload did not invalidate runtime resources");
    }
    for (const auto& record : saved)
    {
        KeelPluginEvent stale{};
        stale.size = sizeof(stale);
        stale.type = record.spec.event;
        stale.sequence = 1000 + record.spec.event;
        stale.plugin = record.spec.event == KEELS2_PLUGIN_EVENT_ALL_LOADED
            ? AllLoadedSnapshot()
            : g_snapshots[1];
        record.spec.callback(&stale, record.spec.user_data);
    }
    if (value(2) != 1 || value(3) != 1 || value(4) != 1 || value(5) != 1 || value(6) != 1)
    {
        return Failure(13, "stale plugin event dispatched after unload");
    }

    ResetHost();
    reset();
    g_service_available = false;
    if (load(&api, 77) != KEEL_FALSE || value(0) != 0)
    {
        return Failure(14, "missing runtime service did not fail before author Load");
    }
    unload(77);
    if (value(1) != 0)
    {
        return Failure(15, "transport unload called author Unload after setup failure");
    }

    ResetHost();
    reset();
    g_fail_subscription = KEELS2_PLUGIN_EVENT_PAUSED;
    if (load(&api, 77) != KEEL_FALSE || value(0) != 0 ||
        g_unsubscribe_count != 2 || !NoActiveSubscriptions())
    {
        return Failure(16, "partial runtime subscription failure did not roll back");
    }
    unload(77);
    if (value(1) != 0)
    {
        return Failure(17, "author Unload ran after partial setup failure");
    }
    return 0;
}
