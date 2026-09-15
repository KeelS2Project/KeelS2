#include "published_service_fixture.h"

#include <keels2/plugin.h>
#include <keels2/services.h>

#include <cstdint>

namespace
{

const KeelHostApi* g_host{};
KeelPluginHandle g_plugin{};

std::int32_t Add(std::int32_t left, std::int32_t right)
{
    return left + right;
}

const KeelTestMathService g_math{
    sizeof(KeelTestMathService),
    KEELS2_TEST_MATH_SERVICE_VERSION,
    &Add
};

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
        "Published Service Provider",
        "KeelS2 Tests",
        "1.0.0",
        "Publishes a versioned fixture service"
    };
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(
    const KeelHostApi* api,
    KeelPluginHandle plugin)
{
    if (!api || api->size != sizeof(KeelHostApi) ||
        api->abi_version != KEELS2_PLUGIN_ABI_VERSION || !api->query_service ||
        !api->log || !plugin)
    {
        return KEEL_FALSE;
    }
    const void* value{};
    if (api->query_service(
            plugin,
            KEELS2_SERVICES_SERVICE_NAME,
            KEELS2_SERVICES_API_VERSION,
            &value) != KEEL_RESULT_OK || !value)
    {
        return KEEL_FALSE;
    }
    const auto* services = static_cast<const KeelServicesApi*>(value);
    if (services->size != sizeof(KeelServicesApi) || !services->publish ||
        !services->withdraw || !services->release)
    {
        return KEEL_FALSE;
    }
    KeelServiceHandle rejected{};
    const KeelServiceSpec reserved{
        sizeof(KeelServiceSpec),
        1,
        "keels2.reserved",
        &g_math
    };
    KeelServiceHandle publication{};
    const KeelServiceSpec spec{
        sizeof(KeelServiceSpec),
        KEELS2_TEST_MATH_SERVICE_VERSION,
        KEELS2_TEST_MATH_SERVICE_NAME,
        &g_math
    };
    if (services->publish(plugin, &reserved, &rejected) != KEEL_RESULT_INVALID_ARGUMENT ||
        rejected || services->publish(plugin, &spec, &publication) != KEEL_RESULT_OK ||
        !publication || services->publish(plugin, &spec, &rejected) !=
            KEEL_RESULT_ALREADY_EXISTS || rejected)
    {
        return KEEL_FALSE;
    }
    g_host = api;
    g_plugin = plugin;
    api->log(plugin, KEEL_LOG_INFO, "versioned service published");
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin)
{
    if (g_host && plugin == g_plugin)
    {
        g_host->log(plugin, KEEL_LOG_INFO, "provider unloaded after publication withdrawal");
    }
    g_host = nullptr;
    g_plugin = 0;
}
