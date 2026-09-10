#include "factory_fixture.h"

#include <cstdlib>
#include <cstring>

namespace
{

FactoryFixture fixture;
const KeelHostApi* host{};

bool ConsumeFlag(const char* name)
{
#if defined(_WIN32)
    char* value{};
    std::size_t length{};
    const bool enabled = _dupenv_s(&value, &length, name) == 0 && value;
    std::free(value);
    _putenv_s(name, "");
#else
    const bool enabled = std::getenv(name) != nullptr;
    unsetenv(name);
#endif
    return enabled;
}

struct Replacement final : FactoryProbe
{
    int Value() override { return 29; }
} replacement;

std::uint32_t Callback(const KeelFactoryRequest* request, KeelFactoryResult* result, void* data)
{
    const auto index = reinterpret_cast<std::uintptr_t>(data);
    fixture.calls.fetch_add(1);
    if (!request || request->size != sizeof(*request) || !result || result->size != sizeof(*result))
    {
        fixture.errors.fetch_add(1);
        return KEELS2_FACTORY_OBSERVE;
    }
    if (index < 4)
    {
        const unsigned step = fixture.order.fetch_add(1) % 4;
        if (step != index)
        {
            fixture.errors.fetch_add(1);
        }
    }
    if (index == 0 && fixture.recurse.load())
    {
        KeelFactoryResult original{sizeof(original), 0, nullptr};
        int code{};
        if (fixture.api->query_original(fixture.plugin, KEELS2_SOURCE2_FACTORY_ENGINE,
                request->interface_name, &original) != KEEL_RESULT_OK ||
            original.instance != request->original_result ||
            original.return_code != request->original_return_code ||
            !fixture.engine || fixture.engine(request->interface_name, &code) != original.instance ||
            code != original.return_code)
        {
            fixture.errors.fetch_add(1);
        }
        KeelSource2InterfaceInfo info{};
        info.size = sizeof(info);
        if (fixture.source2->query_named_interface(fixture.plugin,
                KEELS2_SOURCE2_FACTORY_FILESYSTEM, request->interface_name, &info) != KEEL_RESULT_OK ||
            info.instance != request->original_result)
        {
            fixture.errors.fetch_add(1);
        }
    }
    if (index == 0 && fixture.remove_peer.exchange(false))
    {
        if (fixture.api->unsubscribe(fixture.plugin, fixture.subscriptions[3]) != KEEL_RESULT_OK)
        {
            fixture.errors.fetch_add(1);
        }
    }
    if (index == 0 && fixture.remove_self.exchange(false))
    {
        if (fixture.api->unsubscribe(fixture.plugin, fixture.subscriptions[0]) != KEEL_RESULT_BUSY)
        {
            fixture.errors.fetch_add(1);
        }
    }
    if (index == 4 || (index == 1 && fixture.mode.load() == 3))
    {
        fixture.entered.store(true);
        fixture.entered.notify_all();
        while (!fixture.release.load())
        {
            fixture.release.wait(false);
        }
    }
    if (index == 5 && (request->original_result || request->original_return_code != 17))
    {
        fixture.errors.fetch_add(1);
    }
    if (index == 6 && (request->factory != KEELS2_SOURCE2_FACTORY_SERVER ||
        request->original_result || request->original_return_code != 1))
    {
        fixture.errors.fetch_add(1);
    }
    if (index == 1 && fixture.mode.load())
    {
        result->instance = fixture.mode.load() >= 2 ? &replacement : nullptr;
        result->return_code = fixture.mode.load() >= 2 ? 0 : 42;
        return KEELS2_FACTORY_REPLACE;
    }
    if (index == 2 && fixture.mode.load())
    {
        if (request->current_result != (fixture.mode.load() >= 2 ? &replacement : nullptr) ||
            request->current_return_code != (fixture.mode.load() >= 2 ? 0 : 42))
        {
            fixture.errors.fetch_add(1);
        }
        result->instance = nullptr;
        result->return_code = 91;
        return KEELS2_FACTORY_REPLACE;
    }
    return KEELS2_FACTORY_OBSERVE;
}

}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(
    const KeelHostQuery* query, KeelPluginInfo* info)
{
    if (!query || !info || query->size != sizeof(*query) || info->size != sizeof(*info) ||
        query->abi_version != KEELS2_PLUGIN_ABI_VERSION)
    {
        return KEEL_FALSE;
    }
    const bool wrong_name = ConsumeFlag("KEELS2_FACTORY_WRONG_NAME");
    *info = {sizeof(*info), KEELS2_PLUGIN_ABI_VERSION,
        wrong_name ? "Wrong Factory Test" : "Factory Test", "KeelS2 Project",
        "1", "Managed factory acceptance fixture"};
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(
    const KeelHostApi* api, KeelPluginHandle plugin)
{
    host = api;
    fixture.plugin = plugin;
    const void* value{};
    if (api->query_service(plugin, KEELS2_FACTORIES_SERVICE_NAME, 1, &value) != KEEL_RESULT_OK)
    {
        return KEEL_FALSE;
    }
    fixture.api = static_cast<const KeelFactoriesApi*>(value);
    if (api->query_service(plugin, KEELS2_SOURCE2_SERVICE_NAME, 2, &value) != KEEL_RESULT_OK)
    {
        return KEEL_FALSE;
    }
    fixture.source2 = static_cast<const KeelSource2Api*>(value);
    for (std::size_t index{}; index < fixture.subscriptions.size(); ++index)
    {
        const char* name = index < 4 ? "KeelFactoryProbe001" : index == 4
            ? "KeelFactoryBlock001" : index == 5 ? "KeelFactoryMissing001"
            : "KeelFactoryServerMissing001";
        const KeelFactorySubscriptionSpec spec{sizeof(spec),
            index == 6 ? KEELS2_SOURCE2_FACTORY_SERVER : index == 2
                ? KEELS2_SOURCE2_FACTORY_FILESYSTEM : KEELS2_SOURCE2_FACTORY_ENGINE,
            name, index == 0 ? 100 : index == 3 ? -10 : 50,
            KEELS2_FACTORY_PROCESS_LIFETIME, &Callback, reinterpret_cast<void*>(index)};
        if (fixture.api->subscribe(plugin, &spec, &fixture.subscriptions[index]) != KEEL_RESULT_OK)
        {
            return KEEL_FALSE;
        }
    }
    fixture.mode.store(2);
    KeelSource2InterfaceInfo info{};
    info.size = sizeof(info);
    if (fixture.source2->query_named_interface(plugin, KEELS2_SOURCE2_FACTORY_ENGINE,
            "KeelFactoryProbe001", &info) != KEEL_RESULT_OK ||
        static_cast<FactoryProbe*>(info.instance)->Value() != 11 || fixture.calls.load())
    {
        return KEEL_FALSE;
    }
    fixture.mode.store(0);
    api->log(plugin, KEEL_LOG_INFO, "factory load subscriptions remained inactive");
    const bool fail = ConsumeFlag("KEELS2_FACTORY_FAIL_LOAD");
    return fail ? KEEL_FALSE : KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin)
{
    fixture.unloaded.fetch_add(1);
    if (fixture.query_on_unload.load() && fixture.engine)
    {
        int code{};
        fixture.engine("KeelFactoryProbe001", &code);
    }
    host->log(plugin, KEEL_LOG_INFO, "factory fixture unloaded");
}

extern "C" KEELS2_PLUGIN_EXPORT FactoryFixture* KeelTest_FactoryFixture()
{
    return &fixture;
}
