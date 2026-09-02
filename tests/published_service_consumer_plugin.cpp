#include "published_service_fixture.h"

#include <keels2/plugin.h>
#include <keels2/services.h>

namespace
{

const KeelHostApi* g_host{};
const KeelServicesApi* g_services{};
KeelPluginHandle g_plugin{};
bool g_released{};

void ReleaseCommand(const KeelCommandInvocation*, void*)
{
    if (!g_services || g_released ||
        g_services->release(
            g_plugin,
            KEELS2_TEST_MATH_SERVICE_NAME,
            KEELS2_TEST_MATH_SERVICE_VERSION) != KEEL_RESULT_OK ||
        g_services->release(
            g_plugin,
            KEELS2_TEST_MATH_SERVICE_NAME,
            KEELS2_TEST_MATH_SERVICE_VERSION) != KEEL_RESULT_NOT_FOUND)
    {
        g_host->log(g_plugin, KEEL_LOG_ERROR, "service lease release failed");
        return;
    }
    g_released = true;
    g_host->log(g_plugin, KEEL_LOG_INFO, "service lease released");
}

void VerifyGoneCommand(const KeelCommandInvocation*, void*)
{
    const void* service = reinterpret_cast<const void*>(1);
    if (!g_released || g_host->query_service(
            g_plugin,
            KEELS2_TEST_MATH_SERVICE_NAME,
            KEELS2_TEST_MATH_SERVICE_VERSION,
            &service) != KEEL_RESULT_NOT_FOUND || service)
    {
        g_host->log(g_plugin, KEEL_LOG_ERROR, "withdrawn service remained queryable");
        return;
    }
    g_host->log(g_plugin, KEEL_LOG_INFO, "withdrawn service is no longer queryable");
}

bool RegisterCommand(const char* name, KeelCommandCallback callback)
{
    const KeelCommandSpec spec{
        sizeof(KeelCommandSpec),
        name,
        "Published service integration fixture",
        0,
        callback,
        nullptr
    };
    KeelCommandHandle command{};
    return g_host->register_command(g_plugin, &spec, &command) == KEEL_RESULT_OK && command;
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
        "Published Service Consumer",
        "KeelS2 Tests",
        "1.0.0",
        "Consumes a versioned fixture service"
    };
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(
    const KeelHostApi* api,
    KeelPluginHandle plugin)
{
    if (!api || api->size != sizeof(KeelHostApi) ||
        api->abi_version != KEELS2_PLUGIN_ABI_VERSION || !api->query_service ||
        !api->register_command || !api->log || !plugin)
    {
        return KEEL_FALSE;
    }
    g_host = api;
    g_plugin = plugin;
    g_released = false;
    const void* services{};
    const void* wrong = reinterpret_cast<const void*>(1);
    const void* value{};
    if (api->query_service(
            plugin,
            KEELS2_SERVICES_SERVICE_NAME,
            KEELS2_SERVICES_API_VERSION,
            &services) != KEEL_RESULT_OK || !services ||
        api->query_service(
            plugin,
            KEELS2_TEST_MATH_SERVICE_NAME,
            KEELS2_TEST_MATH_SERVICE_VERSION + 1,
            &wrong) != KEEL_RESULT_INCOMPATIBLE || wrong ||
        api->query_service(
            plugin,
            KEELS2_TEST_MATH_SERVICE_NAME,
            KEELS2_TEST_MATH_SERVICE_VERSION,
            &value) != KEEL_RESULT_OK || !value)
    {
        return KEEL_FALSE;
    }
    g_services = static_cast<const KeelServicesApi*>(services);
    const auto* math = static_cast<const KeelTestMathService*>(value);
    if (math->size != sizeof(KeelTestMathService) ||
        math->version != KEELS2_TEST_MATH_SERVICE_VERSION || !math->add ||
        math->add(20, 22) != 42 ||
        !RegisterCommand("published_release", &ReleaseCommand) ||
        !RegisterCommand("published_verify_gone", &VerifyGoneCommand))
    {
        return KEEL_FALSE;
    }
    api->log(plugin, KEEL_LOG_INFO, "versioned service consumed");
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle)
{
    g_host = nullptr;
    g_services = nullptr;
    g_plugin = 0;
    g_released = false;
}
