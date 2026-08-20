#include <keels2/platform/dynamic_library.h>
#include <keels2/plugin.hpp>
#include <keels2/source2_authoring.h>
#include <tier1/convar.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <type_traits>

namespace
{

KeelSource2CommandCallback g_callback{};
void* g_user_data{};
std::string g_command_name;
std::string g_command_description;
std::string g_log_message;
std::uint32_t g_unregister_count{};
std::uint32_t g_service_query_count{};
std::uint64_t g_command_flags{};

void TestLog(KeelPluginHandle plugin, KeelLogLevel level, const char* message)
{
    if (plugin == 1 && level == KEEL_LOG_INFO && message)
    {
        g_log_message = message;
    }
}

KeelResult TestRegisterCommand(
    KeelPluginHandle plugin,
    const KeelCommandSpec* spec,
    KeelCommandHandle* command)
{
    if (plugin != 1 || !spec || spec->size != sizeof(KeelCommandSpec) ||
        !spec->name || !spec->callback || !command)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    g_command_name = spec->name;
    g_command_description = spec->description ? spec->description : "";
    g_command_flags = spec->flags;
    *command = 1;
    return KEEL_RESULT_OK;
}

KeelResult TestUnregisterCommand(KeelPluginHandle plugin, KeelCommandHandle command)
{
    if (plugin != 1 || command != 1)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    ++g_unregister_count;
    return KEEL_RESULT_OK;
}

KeelResult TestRegisterSource2Command(
    KeelPluginHandle plugin,
    const KeelSource2CommandSpec* spec,
    KeelCommandHandle* command)
{
    if (plugin != 1 || !spec || spec->size != sizeof(KeelSource2CommandSpec) ||
        spec->reserved != 0 || !spec->name || !spec->callback || !command)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }

    g_command_name = spec->name;
    g_command_description = spec->description ? spec->description : "";
    g_command_flags = spec->flags;
    g_callback = spec->callback;
    g_user_data = spec->user_data;
    *command = 1;
    return KEEL_RESULT_OK;
}

KeelResult TestConVarUnavailable(
    KeelPluginHandle,
    const KeelConVarSpec*,
    KeelSource2ConVarChangeCallback,
    void*,
    KeelConVarHandle*,
    void**)
{
    return KEEL_RESULT_NOT_READY;
}

KeelResult TestFindConVarUnavailable(
    KeelPluginHandle,
    const char*,
    KeelConVarType,
    KeelConVarHandle*,
    void**)
{
    return KEEL_RESULT_NOT_READY;
}

const KeelSource2AuthoringApi g_source2_authoring_api{
    sizeof(KeelSource2AuthoringApi),
    KEELS2_SOURCE2_AUTHORING_API_VERSION,
    &TestRegisterSource2Command,
    &TestUnregisterCommand,
    &TestConVarUnavailable,
    &TestFindConVarUnavailable,
    &TestUnregisterCommand
};

KeelResult TestQueryService(
    KeelPluginHandle plugin,
    const char* name,
    std::uint32_t version,
    const void** service)
{
    ++g_service_query_count;
    if (!plugin || !name || !service)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *service = nullptr;
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

}

static_assert(!std::is_copy_constructible_v<keels2::Command>);
static_assert(!std::is_copy_assignable_v<keels2::Command>);
static_assert(std::is_move_constructible_v<keels2::Command>);
static_assert(std::is_move_assignable_v<keels2::Command>);

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
        sizeof(KeelHostQuery),
        KEELS2_PLUGIN_ABI_VERSION,
        "test-host",
        "cs2",
#if defined(_WIN32)
        "win64"
#else
        "linuxsteamrt64"
#endif
    };
    KeelPluginInfo info{};
    info.size = sizeof(info);
    if (query(&host_query, &info) != KEEL_TRUE ||
        info.abi_version != KEELS2_PLUGIN_ABI_VERSION ||
        !info.name || std::strcmp(info.name, "KeelS2 Basic") != 0)
    {
        return 4;
    }

    const KeelHostApi api{
        sizeof(KeelHostApi),
        KEELS2_PLUGIN_ABI_VERSION,
        &TestLog,
        &TestRegisterCommand,
        &TestUnregisterCommand,
        &TestQueryService
    };
    if (load(&api, 1) != KEEL_TRUE || g_command_name != "keel_test" ||
        g_command_description != "Verifies the KeelS2 native plugin command path" ||
        g_command_flags != 0 || !g_callback || g_service_query_count != 1)
    {
        return 5;
    }

    const char* invocation_arguments[]{"keel_test", "second"};
    const CCommandContext context{CT_NO_TARGET, CPlayerSlot{-1}};
    const CCommand invocation{2, invocation_arguments};
    g_callback(&context, &invocation, g_user_data);
    if (g_log_message != "KeelS2 0.1.0-dev is active. The basic native plugin is responding.")
    {
        return 6;
    }

    const char* unregister_arguments[]{"keel_test", "unregister"};
    const CCommand unregister_invocation{2, unregister_arguments};
    g_callback(&context, &unregister_invocation, g_user_data);
    if (g_unregister_count != 1 || g_log_message != "keel_test command unregistered.")
    {
        return 7;
    }

    unload(1);
    if (g_log_message != "unload callback completed")
    {
        return 8;
    }

    return 0;
}
