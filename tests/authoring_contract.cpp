#include <keels2/lifecycle.h>
#include <keels2/platform/dynamic_library.h>
#include <keels2/plugin.h>
#include <keels2/source2.h>
#include <keels2/source2_authoring.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{

struct LifecycleRecord
{
    KeelLifecycleSubscriptionSpec spec{};
    KeelLifecycleSubscriptionHandle handle{};
    KeelPluginHandle owner{};
    bool active{};
};

struct CommandRecord
{
    std::string name;
    std::uint64_t flags{};
    KeelCommandCallback legacy_callback{};
    KeelSource2CommandCallback callback{};
    void* user_data{};
    KeelCommandHandle handle{};
    KeelPluginHandle owner{};
    bool active{};
};

std::vector<LifecycleRecord> g_lifecycle_records;
std::vector<CommandRecord> g_command_records;
std::vector<std::pair<KeelLogLevel, std::string>> g_logs;
KeelLifecycleSubscriptionHandle g_next_subscription{1};
KeelCommandHandle g_next_command{1};
std::uint32_t g_unsubscribe_count{};
std::uint32_t g_unregister_count{};
std::uint32_t g_lifecycle_queries{};
std::uint32_t g_source2_queries{};
bool g_wrong_provenance{};
bool g_dispatch_during_subscribe{};
bool g_lifecycle_unavailable{};
bool g_fail_command_registration{};
KeelLifecycleEventType g_fail_subscription_event{};
int g_server{};
int g_game_clients{};
int g_cvar{};

void ResetHost()
{
    g_lifecycle_records.clear();
    g_command_records.clear();
    g_logs.clear();
    g_next_subscription = 1;
    g_next_command = 1;
    g_unsubscribe_count = 0;
    g_unregister_count = 0;
    g_lifecycle_queries = 0;
    g_source2_queries = 0;
    g_wrong_provenance = false;
    g_dispatch_during_subscribe = true;
    g_lifecycle_unavailable = false;
    g_fail_command_registration = false;
    g_fail_subscription_event = 0;
}

bool HasLog(KeelLogLevel level, const char* text)
{
    for (const auto& [entry_level, message] : g_logs)
    {
        if (entry_level == level && message.find(text) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

LifecycleRecord* LifecycleByEvent(KeelLifecycleEventType event)
{
    for (auto& record : g_lifecycle_records)
    {
        if (record.active && record.spec.event == event)
        {
            return &record;
        }
    }
    return nullptr;
}

CommandRecord* CommandByName(const char* name)
{
    for (auto& record : g_command_records)
    {
        if (record.name == name)
        {
            return &record;
        }
    }
    return nullptr;
}

bool AllResourcesReleased()
{
    for (const auto& record : g_lifecycle_records)
    {
        if (record.active)
        {
            return false;
        }
    }
    for (const auto& record : g_command_records)
    {
        if (record.active)
        {
            return false;
        }
    }
    return true;
}

int Failure(int code, const char* message)
{
    std::fprintf(stderr, "authoring contract failure: %s\n", message);
    return code;
}

void Log(KeelPluginHandle, KeelLogLevel level, const char* message)
{
    if (message)
    {
        g_logs.emplace_back(level, message);
    }
}

KeelResult RegisterCommand(
    KeelPluginHandle plugin,
    const KeelCommandSpec* spec,
    KeelCommandHandle* command)
{
    if (!plugin || !spec || spec->size != sizeof(KeelCommandSpec) || !spec->name ||
        !spec->callback || !command)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (g_fail_command_registration)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    for (const auto& record : g_command_records)
    {
        if (record.active && record.name == spec->name)
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
    }
    CommandRecord record;
    record.name = spec->name;
    record.flags = spec->flags;
    record.legacy_callback = spec->callback;
    record.user_data = spec->user_data;
    record.handle = g_next_command++;
    record.owner = plugin;
    record.active = true;
    *command = record.handle;
    g_command_records.push_back(std::move(record));
    return KEEL_RESULT_OK;
}

KeelResult RegisterSource2Command(
    KeelPluginHandle plugin,
    const KeelSource2CommandSpec* spec,
    KeelCommandHandle* command)
{
    if (!plugin || !spec || spec->size != sizeof(KeelSource2CommandSpec) ||
        spec->reserved != 0 || !spec->name || !spec->callback || !command)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (g_fail_command_registration)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    for (const auto& record : g_command_records)
    {
        if (record.active && record.name == spec->name)
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
    }
    CommandRecord record;
    record.name = spec->name;
    record.flags = spec->flags;
    record.callback = spec->callback;
    record.user_data = spec->user_data;
    record.handle = g_next_command++;
    record.owner = plugin;
    record.active = true;
    *command = record.handle;
    g_command_records.push_back(std::move(record));
    return KEEL_RESULT_OK;
}

KeelResult NativeConVarUnavailable(
    KeelPluginHandle,
    const KeelConVarSpec*,
    KeelSource2ConVarChangeCallback,
    void*,
    KeelConVarHandle*,
    void**)
{
    return KEEL_RESULT_NOT_READY;
}

KeelResult NativeConVarFindUnavailable(
    KeelPluginHandle,
    const char*,
    KeelConVarType,
    KeelConVarHandle*,
    void**)
{
    return KEEL_RESULT_NOT_READY;
}

KeelResult UnregisterCommand(KeelPluginHandle plugin, KeelCommandHandle command)
{
    for (auto& record : g_command_records)
    {
        if (record.handle == command && record.owner == plugin)
        {
            if (!record.active)
            {
                return KEEL_RESULT_NOT_FOUND;
            }
            record.active = false;
            ++g_unregister_count;
            return KEEL_RESULT_OK;
        }
    }
    return KEEL_RESULT_NOT_FOUND;
}

KeelResult Subscribe(
    KeelPluginHandle plugin,
    const KeelLifecycleSubscriptionSpec* spec,
    KeelLifecycleSubscriptionHandle* subscription)
{
    if (!plugin || !spec || spec->size != sizeof(KeelLifecycleSubscriptionSpec) ||
        spec->reserved != 0 || !spec->callback || !subscription)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (spec->event == g_fail_subscription_event)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    LifecycleRecord record;
    record.spec = *spec;
    record.handle = g_next_subscription++;
    record.owner = plugin;
    record.active = true;
    *subscription = record.handle;
    g_lifecycle_records.push_back(record);
    if (g_dispatch_during_subscribe)
    {
        if (spec->event == KEELS2_LIFECYCLE_CLIENT_ACTIVE)
        {
            const KeelLifecycleClientActive payload{
                sizeof(KeelLifecycleClientActive), 4, 76561198000000004ull,
                "Keel", KEEL_FALSE, 0
            };
            const KeelLifecycleEvent event{
                sizeof(KeelLifecycleEvent), spec->event, sizeof(payload), 0, &payload
            };
            spec->callback(&event, spec->user_data);
        }
        else if (spec->event == KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED)
        {
            const KeelLifecycleClientSettingsChanged payload{
                sizeof(KeelLifecycleClientSettingsChanged), 4
            };
            const KeelLifecycleEvent event{
                sizeof(KeelLifecycleEvent), spec->event, sizeof(payload), 0, &payload
            };
            spec->callback(&event, spec->user_data);
        }
    }
    return KEEL_RESULT_OK;
}

KeelResult Unsubscribe(
    KeelPluginHandle plugin,
    KeelLifecycleSubscriptionHandle subscription)
{
    for (auto& record : g_lifecycle_records)
    {
        if (record.handle == subscription && record.owner == plugin)
        {
            if (!record.active)
            {
                return KEEL_RESULT_NOT_FOUND;
            }
            record.active = false;
            ++g_unsubscribe_count;
            return KEEL_RESULT_OK;
        }
    }
    return KEEL_RESULT_NOT_FOUND;
}

KeelResult QuerySource2(
    KeelPluginHandle plugin,
    KeelSource2Capability capability,
    KeelSource2InterfaceInfo* info)
{
    if (!plugin || !info || info->size != sizeof(KeelSource2InterfaceInfo))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    ++g_source2_queries;
    void* instance{};
    const char* interface_name{};
    const char* module_name{};
    KeelSource2Factory factory{};
    switch (capability)
    {
    case KEELS2_SOURCE2_CAPABILITY_SERVER:
        instance = &g_server;
        interface_name = "Source2Server001";
        module_name = "server";
        factory = KEELS2_SOURCE2_FACTORY_SERVER;
        break;
    case KEELS2_SOURCE2_CAPABILITY_GAME_CLIENTS:
        instance = &g_game_clients;
        interface_name = "Source2GameClients001";
        module_name = "server";
        factory = KEELS2_SOURCE2_FACTORY_SERVER;
        break;
    case KEELS2_SOURCE2_CAPABILITY_CVAR:
        instance = &g_cvar;
        interface_name = "VEngineCvar007";
        module_name = "tier0";
        factory = g_wrong_provenance
            ? KEELS2_SOURCE2_FACTORY_SERVER
            : KEELS2_SOURCE2_FACTORY_ENGINE;
        break;
    default:
        return KEEL_RESULT_UNSUPPORTED;
    }
    *info = {
        sizeof(KeelSource2InterfaceInfo), capability, factory,
        KEELS2_SOURCE2_OWNERSHIP_BORROWED, KEELS2_SOURCE2_LIFETIME_HOST, 0,
        instance, interface_name, module_name, "/fixture/module", "fixture-profile"
    };
    return KEEL_RESULT_OK;
}

const KeelLifecycleApi g_lifecycle_api{
    sizeof(KeelLifecycleApi), KEELS2_LIFECYCLE_API_VERSION, &Subscribe, &Unsubscribe
};

const KeelSource2Api g_source2_api{
    sizeof(KeelSource2Api), KEELS2_SOURCE2_API_VERSION, &QuerySource2
};

const KeelSource2AuthoringApi g_source2_authoring_api{
    sizeof(KeelSource2AuthoringApi),
    KEELS2_SOURCE2_AUTHORING_API_VERSION,
    &RegisterSource2Command,
    &UnregisterCommand,
    &NativeConVarUnavailable,
    &NativeConVarFindUnavailable,
    &UnregisterCommand
};

KeelResult QueryService(
    KeelPluginHandle plugin,
    const char* name,
    std::uint32_t version,
    const void** service)
{
    if (!plugin || !name || !service)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *service = nullptr;
    if (std::strcmp(name, KEELS2_LIFECYCLE_SERVICE_NAME) == 0)
    {
        ++g_lifecycle_queries;
        if (g_lifecycle_unavailable)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        if (version != KEELS2_LIFECYCLE_API_VERSION)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        *service = &g_lifecycle_api;
        return KEEL_RESULT_OK;
    }
    if (std::strcmp(name, KEELS2_SOURCE2_SERVICE_NAME) == 0)
    {
        if (version != KEELS2_SOURCE2_API_VERSION)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        *service = &g_source2_api;
        return KEEL_RESULT_OK;
    }
    if (std::strcmp(name, KEELS2_SOURCE2_AUTHORING_SERVICE_NAME) == 0)
    {
        if (version != KEELS2_SOURCE2_AUTHORING_API_VERSION)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        *service = &g_source2_authoring_api;
        return KEEL_RESULT_OK;
    }
    return KEEL_RESULT_NOT_FOUND;
}

template <typename Type>
Type Symbol(keels2::platform::DynamicLibrary& library, const char* name)
{
    return reinterpret_cast<Type>(library.Symbol(name));
}

}

int main(int argument_count, char** arguments)
{
    if (argument_count != 2)
    {
        return Failure(1, "plugin path is required");
    }
    keels2::platform::DynamicLibrary library;
    std::string error;
    if (!library.Open(std::filesystem::path(arguments[1]), error))
    {
        return Failure(2, "plugin could not be opened");
    }

    const auto query = Symbol<KeelPluginQueryFn>(library, "KeelPlugin_Query");
    const auto load = Symbol<KeelPluginLoadFn>(library, "KeelPlugin_Load");
    const auto unload = Symbol<KeelPluginUnloadFn>(library, "KeelPlugin_Unload");
    using ResetFn = void (*)();
    using SetModeFn = void (*)(std::uint32_t);
    using CountFn = std::uint32_t (*)();
    using BoolFn = KeelBool (*)();
    using CreateCommandFn = void* (*)(const char*);
    using DestroyCommandFn = void (*)(void*);
    using CreateContextFn = void* (*)();
    using DestroyContextFn = void (*)(void*);
    const auto reset = Symbol<ResetFn>(library, "KeelTest_AuthoringReset");
    const auto set_mode = Symbol<SetModeFn>(library, "KeelTest_AuthoringSetMode");
    const auto load_count = Symbol<CountFn>(library, "KeelTest_AuthoringLoadCount");
    const auto unload_count = Symbol<CountFn>(library, "KeelTest_AuthoringUnloadCount");
    const auto active_count = Symbol<CountFn>(library, "KeelTest_AuthoringActiveCount");
    const auto settings_count = Symbol<CountFn>(library, "KeelTest_AuthoringSettingsCount");
    const auto self_remove_count = Symbol<CountFn>(library, "KeelTest_AuthoringSelfRemoveCount");
    const auto throw_command_count = Symbol<CountFn>(library, "KeelTest_AuthoringThrowCommandCount");
    const auto source2_ready = Symbol<BoolFn>(library, "KeelTest_AuthoringSource2Ready");
    const auto unload_source2_null = Symbol<BoolFn>(library, "KeelTest_AuthoringUnloadSource2Null");
    const auto active_arguments_valid = Symbol<BoolFn>(library, "KeelTest_AuthoringActiveArgumentsValid");
    const auto settings_arguments_valid = Symbol<BoolFn>(library, "KeelTest_AuthoringSettingsArgumentsValid");
    const auto self_remove_succeeded = Symbol<BoolFn>(library, "KeelTest_AuthoringSelfRemoveSucceeded");
    const auto create_command = Symbol<CreateCommandFn>(library, "KeelTest_AuthoringCreateCommand");
    const auto destroy_command = Symbol<DestroyCommandFn>(library, "KeelTest_AuthoringDestroyCommand");
    const auto create_context = Symbol<CreateContextFn>(library, "KeelTest_AuthoringCreateCommandContext");
    const auto destroy_context = Symbol<DestroyContextFn>(library, "KeelTest_AuthoringDestroyCommandContext");
    if (!query || !load || !unload || !reset || !set_mode || !load_count || !unload_count ||
        !active_count || !settings_count || !self_remove_count || !throw_command_count ||
        !source2_ready || !unload_source2_null || !active_arguments_valid ||
        !settings_arguments_valid || !self_remove_succeeded || !create_command ||
        !destroy_command || !create_context || !destroy_context)
    {
        return Failure(3, "required plugin symbols are missing");
    }

    const KeelHostApi api{
        sizeof(KeelHostApi), KEELS2_PLUGIN_ABI_VERSION, &Log,
        &RegisterCommand, &UnregisterCommand, &QueryService
    };

    ResetHost();
    reset();
    set_mode(4);
    if (load(&api, 99) != KEEL_FALSE || load_count() != 0 || !AllResourcesReleased())
    {
        return Failure(21, "constructor exception escaped the load boundary");
    }
    unload(99);
    set_mode(0);

    const KeelHostQuery host_query{
        sizeof(KeelHostQuery), KEELS2_PLUGIN_ABI_VERSION, "test-host", "cs2",
#if defined(_WIN32)
        "win64"
#else
        "linuxsteamrt64"
#endif
    };
    KeelHostQuery invalid_host_query = host_query;
    invalid_host_query.size = 0;
    KeelPluginInfo invalid_info{};
    invalid_info.size = sizeof(invalid_info);
    if (query(&invalid_host_query, &invalid_info) != KEEL_FALSE)
    {
        return Failure(4, "invalid query envelope was accepted");
    }
    invalid_info.size = 0;
    if (query(&host_query, &invalid_info) != KEEL_FALSE)
    {
        return Failure(5, "invalid metadata envelope was accepted");
    }
    set_mode(4);
    invalid_info.size = sizeof(invalid_info);
    if (query(&host_query, &invalid_info) != KEEL_TRUE || !invalid_info.name ||
        std::strcmp(invalid_info.name, "Authoring Contract") != 0)
    {
        return Failure(6, "static metadata query constructed the plugin");
    }
    set_mode(0);
    KeelPluginInfo info{};
    info.size = sizeof(info);
    if (query(&host_query, &info) != KEEL_TRUE || !info.name ||
        std::strcmp(info.name, "Authoring Contract") != 0)
    {
        return Failure(7, "facade query failed");
    }
    ResetHost();
    reset();
    if (load(&api, 1) != KEEL_TRUE || load_count() != 1 || source2_ready() != KEEL_TRUE ||
        g_source2_queries != 3)
    {
        return Failure(8, "successful facade load failed");
    }
    if (g_lifecycle_queries != 1 || g_lifecycle_records.size() != 2 ||
        g_lifecycle_records[0].spec.event != KEELS2_LIFECYCLE_CLIENT_ACTIVE ||
        g_lifecycle_records[1].spec.event != KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED ||
        active_count() != 0 || settings_count() != 0)
    {
        return Failure(9, "selective staged lifecycle subscriptions failed");
    }

    LifecycleRecord* active = LifecycleByEvent(KEELS2_LIFECYCLE_CLIENT_ACTIVE);
    LifecycleRecord* settings = LifecycleByEvent(KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED);
    if (!active || !settings)
    {
        return Failure(10, "expected lifecycle subscriptions are unavailable");
    }
    const KeelLifecycleClientActive malformed_payload{
        0, 4, 76561198000000004ull, "Keel", KEEL_FALSE, 0
    };
    const KeelLifecycleEvent malformed_event{
        sizeof(KeelLifecycleEvent), KEELS2_LIFECYCLE_CLIENT_ACTIVE,
        sizeof(malformed_payload), 0, &malformed_payload
    };
    active->spec.callback(&malformed_event, active->spec.user_data);
    const KeelLifecycleClientActive invalid_bool_payload{
        sizeof(KeelLifecycleClientActive), 4, 76561198000000004ull, "Keel", 2, 0
    };
    const KeelLifecycleEvent invalid_bool_event{
        sizeof(KeelLifecycleEvent), KEELS2_LIFECYCLE_CLIENT_ACTIVE,
        sizeof(invalid_bool_payload), 0, &invalid_bool_payload
    };
    active->spec.callback(&invalid_bool_event, active->spec.user_data);
    const KeelLifecycleClientActive invalid_reserved_payload{
        sizeof(KeelLifecycleClientActive), 4, 76561198000000004ull, "Keel", KEEL_FALSE, 1
    };
    const KeelLifecycleEvent invalid_reserved_payload_event{
        sizeof(KeelLifecycleEvent), KEELS2_LIFECYCLE_CLIENT_ACTIVE,
        sizeof(invalid_reserved_payload), 0, &invalid_reserved_payload
    };
    active->spec.callback(&invalid_reserved_payload_event, active->spec.user_data);
    if (active_count() != 0)
    {
        return Failure(11, "malformed lifecycle payload was dispatched");
    }

    const KeelLifecycleClientActive throwing_payload{
        sizeof(KeelLifecycleClientActive), 4, 76561198000000004ull, "throw", KEEL_FALSE, 0
    };
    const KeelLifecycleEvent throwing_event{
        sizeof(KeelLifecycleEvent), KEELS2_LIFECYCLE_CLIENT_ACTIVE,
        sizeof(throwing_payload), 0, &throwing_payload
    };
    active->spec.callback(&throwing_event, active->spec.user_data);
    if (active_count() != 1 || !HasLog(KEEL_LOG_ERROR, "lifecycle callback"))
    {
        return Failure(12, "lifecycle exception was not contained");
    }
    const KeelLifecycleClientActive valid_payload{
        sizeof(KeelLifecycleClientActive), 4, 76561198000000004ull, "Keel", KEEL_FALSE, 0
    };
    const KeelLifecycleEvent invalid_reserved_event{
        sizeof(KeelLifecycleEvent), KEELS2_LIFECYCLE_CLIENT_ACTIVE,
        sizeof(valid_payload), 1, &valid_payload
    };
    active->spec.callback(&invalid_reserved_event, active->spec.user_data);
    const KeelLifecycleEvent valid_event{
        sizeof(KeelLifecycleEvent), KEELS2_LIFECYCLE_CLIENT_ACTIVE,
        sizeof(valid_payload), 0, &valid_payload
    };
    settings->spec.callback(&valid_event, settings->spec.user_data);
    active->spec.callback(&valid_event, active->spec.user_data);
    const KeelLifecycleClientSettingsChanged settings_payload{
        sizeof(KeelLifecycleClientSettingsChanged), 4
    };
    const KeelLifecycleEvent settings_event{
        sizeof(KeelLifecycleEvent), KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED,
        sizeof(settings_payload), 0, &settings_payload
    };
    settings->spec.callback(&settings_event, settings->spec.user_data);
    if (active_count() != 2 || settings_count() != 1 ||
        active_arguments_valid() != KEEL_TRUE || settings_arguments_valid() != KEEL_TRUE)
    {
        return Failure(13, "lifecycle arguments were mapped incorrectly");
    }

    CommandRecord* self_remove = CommandByName("authoring_self_remove");
    CommandRecord* throwing_command = CommandByName("authoring_throw");
    if (!self_remove || !throwing_command || self_remove->flags != 0 ||
        throwing_command->flags != std::uint64_t{0x2000})
    {
        return Failure(14, "facade commands or raw flags were not registered");
    }
    const KeelSource2CommandCallback saved_self_callback = self_remove->callback;
    void* const saved_self_user_data = self_remove->user_data;
    void* command_context = create_context();
    void* self_invocation = create_command("authoring_self_remove");
    if (!command_context || !self_invocation)
    {
        return Failure(15, "Source 2 command fixture could not be created");
    }
    saved_self_callback(command_context, self_invocation, saved_self_user_data);
    if (self_remove_count() != 1 || self_remove_succeeded() != KEEL_TRUE ||
        self_remove->active || g_unregister_count != 1)
    {
        return Failure(15, "self-removing command failed");
    }
    saved_self_callback(command_context, self_invocation, saved_self_user_data);
    destroy_command(self_invocation);
    if (self_remove_count() != 1)
    {
        return Failure(16, "removed command binding remained dispatchable");
    }
    void* throwing_invocation = create_command("authoring_throw");
    if (!throwing_invocation)
    {
        return Failure(17, "throwing Source 2 command fixture could not be created");
    }
    throwing_command->callback(
        command_context,
        throwing_invocation,
        throwing_command->user_data);
    throwing_command->callback(
        command_context,
        throwing_invocation,
        throwing_command->user_data);
    destroy_command(throwing_invocation);
    destroy_context(command_context);
    if (throw_command_count() != 2 || !HasLog(KEEL_LOG_ERROR, "command callback"))
    {
        return Failure(17, "command exception was not contained");
    }

    unload(1);
    if (unload_count() != 1 || unload_source2_null() != KEEL_TRUE ||
        g_unsubscribe_count != 2 || g_unregister_count != 2 || !AllResourcesReleased())
    {
        return Failure(18, "successful facade unload was incomplete");
    }

    ResetHost();
    reset();
    g_wrong_provenance = true;
    if (load(&api, 2) != KEEL_TRUE || source2_ready() != KEEL_FALSE ||
        g_source2_queries != 3)
    {
        return Failure(19, "incorrect Source2 provenance was accepted");
    }
    unload(2);
    if (unload_count() != 1 || unload_source2_null() != KEEL_TRUE || !AllResourcesReleased())
    {
        return Failure(20, "provenance-test unload failed");
    }

    ResetHost();
    reset();
    KeelHostApi invalid_api = api;
    invalid_api.query_service = nullptr;
    if (load(&invalid_api, 3) != KEEL_FALSE || load_count() != 0 ||
        g_lifecycle_queries != 0 || !AllResourcesReleased())
    {
        return Failure(22, "invalid host API was accepted");
    }
    unload(3);
    if (unload_count() != 0)
    {
        return Failure(23, "author Unload ran after invalid host API rejection");
    }

    ResetHost();
    reset();
    g_lifecycle_unavailable = true;
    if (load(&api, 4) != KEEL_FALSE || load_count() != 0 ||
        g_lifecycle_queries != 1 || !AllResourcesReleased())
    {
        return Failure(24, "missing lifecycle service was accepted");
    }
    unload(4);
    if (unload_count() != 0)
    {
        return Failure(25, "author Unload ran after lifecycle setup failure");
    }

    ResetHost();
    reset();
    g_fail_subscription_event = KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED;
    if (load(&api, 5) != KEEL_FALSE || load_count() != 0 ||
        g_lifecycle_records.size() != 1 || g_unsubscribe_count != 1 ||
        !AllResourcesReleased())
    {
        return Failure(26, "partial lifecycle setup was not rolled back");
    }
    unload(5);
    if (unload_count() != 0)
    {
        return Failure(27, "author Unload ran after partial lifecycle setup failure");
    }

    ResetHost();
    reset();
    g_fail_command_registration = true;
    if (load(&api, 6) != KEEL_FALSE || load_count() != 1 ||
        g_unsubscribe_count != 2 || g_unregister_count != 0 ||
        !AllResourcesReleased())
    {
        return Failure(28, "command registration failure was not rolled back");
    }
    unload(6);
    if (unload_count() != 0)
    {
        return Failure(29, "author Unload ran after command registration failure");
    }

    ResetHost();
    reset();
    set_mode(1);
    if (load(&api, 7) != KEEL_FALSE || load_count() != 1 || unload_count() != 0 ||
        g_unsubscribe_count != 2 || g_unregister_count != 2 || !AllResourcesReleased())
    {
        return Failure(30, "false Load rollback failed");
    }
    unload(7);
    if (unload_count() != 0)
    {
        return Failure(31, "author Unload ran after false Load");
    }

    ResetHost();
    reset();
    set_mode(2);
    if (load(&api, 8) != KEEL_FALSE || load_count() != 1 || unload_count() != 0 ||
        g_unsubscribe_count != 2 || g_unregister_count != 2 || !AllResourcesReleased())
    {
        return Failure(32, "throwing Load rollback failed");
    }
    unload(8);
    if (unload_count() != 0)
    {
        return Failure(33, "author Unload ran after throwing Load");
    }

    ResetHost();
    reset();
    if (load(&api, 9) != KEEL_TRUE)
    {
        return Failure(34, "unload-exception fixture failed to load");
    }
    set_mode(5);
    unload(9);
    if (unload_count() != 1 || unload_source2_null() != KEEL_TRUE ||
        !HasLog(KEEL_LOG_ERROR, "unload callback") || !AllResourcesReleased())
    {
        return Failure(35, "author Unload exception was not contained");
    }

    return 0;
}
