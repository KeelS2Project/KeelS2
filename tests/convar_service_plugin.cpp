#include <keels2/convar.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{

const KeelHostApi* g_host{};
const KeelConVarApi* g_convars{};
KeelPluginHandle g_plugin{};
KeelConVarHandle g_integer{};
std::atomic<std::uint32_t> g_callback_count{};
std::atomic<std::uint32_t> g_invalid_count{};
std::atomic<std::uint32_t> g_busy_count{};
std::atomic<std::uint32_t> g_unload_count{};
std::atomic<bool> g_block_armed{};
std::atomic<bool> g_block_entered{};
std::atomic<bool> g_block_release{};
bool g_loaded{};

void Log(KeelLogLevel level, const char* message)
{
    if (g_host && g_host->log)
    {
        g_host->log(g_plugin, level, message);
    }
}

KeelConVarValue BoolValue(KeelBool value)
{
    return {sizeof(KeelConVarValue), KEELS2_CONVAR_BOOL, {.boolean_value = value}};
}

KeelConVarValue IntValue(std::int32_t value)
{
    return {sizeof(KeelConVarValue), KEELS2_CONVAR_INT32, {.int32_value = value}};
}

KeelConVarValue FloatValue(float value)
{
    return {sizeof(KeelConVarValue), KEELS2_CONVAR_FLOAT32, {.float32_value = value}};
}

KeelConVarValue StringValue(const char* value)
{
    return {sizeof(KeelConVarValue), KEELS2_CONVAR_STRING, {.string_value = value}};
}

void Changed(const KeelConVarChange* change, void*)
{
    const bool valid = change && change->size == sizeof(KeelConVarChange) &&
        change->slot == KEELS2_CONVAR_GLOBAL_SLOT && change->convar == g_integer &&
        change->name && std::strcmp(change->name, "keels2_test_int") == 0 &&
        change->old_value.size == sizeof(KeelConVarValue) &&
        change->old_value.type == KEELS2_CONVAR_INT32 &&
        change->new_value.size == sizeof(KeelConVarValue) &&
        change->new_value.type == KEELS2_CONVAR_INT32;
    if (!valid)
    {
        g_invalid_count.fetch_add(1, std::memory_order_acq_rel);
        return;
    }

    const std::uint32_t count = g_callback_count.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (count == 1 && g_convars &&
        g_convars->release(g_plugin, g_integer) == KEEL_RESULT_BUSY)
    {
        g_busy_count.fetch_add(1, std::memory_order_acq_rel);
        Log(KEEL_LOG_INFO, "self release returned busy without disabling the ConVar");
    }
    if (count == 1 && change->new_value.value.int32_value == 9 && g_convars)
    {
        const KeelConVarValue deferred = IntValue(11);
        if (g_convars->queue_set(
                g_plugin,
                g_integer,
                KEELS2_CONVAR_GLOBAL_SLOT,
                &deferred) != KEEL_RESULT_OK)
        {
            g_invalid_count.fetch_add(1, std::memory_order_acq_rel);
        }
    }
    if (g_block_armed.exchange(false, std::memory_order_acq_rel))
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
    if (change->new_value.value.int32_value == 10)
    {
        throw std::runtime_error("intentional ConVar callback exception");
    }
}

KeelConVarSpec IntegerSpec(std::int32_t default_value)
{
    KeelConVarSpec spec{};
    spec.size = sizeof(spec);
    spec.type = KEELS2_CONVAR_INT32;
    spec.name = "keels2_test_int";
    spec.description = "KeelS2 ConVar integration integer";
    spec.flags = KEELS2_CVAR_FLAG_NOTIFY;
    spec.default_value = IntValue(default_value);
    spec.has_minimum = KEEL_TRUE;
    spec.minimum_value = IntValue(1);
    spec.has_maximum = KEEL_TRUE;
    spec.maximum_value = IntValue(11);
    spec.callback = &Changed;
    return spec;
}

bool CreateFixtures()
{
    const bool incompatible = std::getenv("KEELS2_TEST_CONVAR_INCOMPATIBLE") != nullptr;
    KeelConVarSpec integer = IntegerSpec(incompatible ? 6 : 7);
    const KeelResult integer_result = g_convars->create(g_plugin, &integer, &g_integer);
    if (incompatible)
    {
        if (integer_result == KEEL_RESULT_INCOMPATIBLE && g_integer == 0)
        {
            Log(KEEL_LOG_INFO, "incompatible persistent ConVar definition was rejected");
        }
        return false;
    }
    if (integer_result != KEEL_RESULT_OK || g_integer == 0)
    {
        return false;
    }

    KeelConVarHandle duplicate = 999;
    if (g_convars->create(g_plugin, &integer, &duplicate) != KEEL_RESULT_ALREADY_EXISTS ||
        duplicate != 0)
    {
        return false;
    }

    KeelConVarSpec boolean{};
    boolean.size = sizeof(boolean);
    boolean.type = KEELS2_CONVAR_BOOL;
    boolean.name = "keels2_test_bool";
    boolean.description = "KeelS2 ConVar integration boolean";
    boolean.default_value = BoolValue(KEEL_TRUE);
    KeelConVarHandle boolean_handle{};
    if (g_convars->create(g_plugin, &boolean, &boolean_handle) != KEEL_RESULT_OK)
    {
        return false;
    }

    KeelConVarSpec floating{};
    floating.size = sizeof(floating);
    floating.type = KEELS2_CONVAR_FLOAT32;
    floating.name = "keels2_test_float";
    floating.description = "KeelS2 ConVar integration float";
    floating.default_value = FloatValue(1.5F);
    floating.has_minimum = KEEL_TRUE;
    floating.minimum_value = FloatValue(0.5F);
    floating.has_maximum = KEEL_TRUE;
    floating.maximum_value = FloatValue(2.5F);
    KeelConVarHandle float_handle{};
    if (g_convars->create(g_plugin, &floating, &float_handle) != KEEL_RESULT_OK)
    {
        return false;
    }

    KeelConVarSpec string{};
    string.size = sizeof(string);
    string.type = KEELS2_CONVAR_STRING;
    string.name = "keels2_test_string";
    string.description = "KeelS2 ConVar integration string";
    string.default_value = StringValue("keels2-default");
    KeelConVarHandle string_handle{};
    if (g_convars->create(g_plugin, &string, &string_handle) != KEEL_RESULT_OK)
    {
        return false;
    }

    const KeelConVarValue boolean_changed = BoolValue(KEEL_FALSE);
    const KeelConVarValue floating_changed = FloatValue(9.0F);
    const KeelConVarValue string_changed = StringValue("keels2-changed");
    KeelConVarValue boolean_read{};
    KeelConVarValue floating_read{};
    KeelConVarValue string_read{};
    boolean_read.size = sizeof(boolean_read);
    floating_read.size = sizeof(floating_read);
    string_read.size = sizeof(string_read);
    if (g_convars->queue_set(
            g_plugin,
            boolean_handle,
            KEELS2_CONVAR_GLOBAL_SLOT,
            &boolean_changed) != KEEL_RESULT_OK ||
        g_convars->read(
            g_plugin,
            boolean_handle,
            KEELS2_CONVAR_GLOBAL_SLOT,
            &boolean_read) != KEEL_RESULT_OK ||
        boolean_read.value.boolean_value != KEEL_FALSE ||
        g_convars->queue_set(
            g_plugin,
            float_handle,
            KEELS2_CONVAR_GLOBAL_SLOT,
            &floating_changed) != KEEL_RESULT_OK ||
        g_convars->read(
            g_plugin,
            float_handle,
            KEELS2_CONVAR_GLOBAL_SLOT,
            &floating_read) != KEEL_RESULT_OK ||
        floating_read.value.float32_value != 2.5F ||
        g_convars->queue_set(
            g_plugin,
            string_handle,
            KEELS2_CONVAR_GLOBAL_SLOT,
            &string_changed) != KEEL_RESULT_OK ||
        g_convars->read(
            g_plugin,
            string_handle,
            KEELS2_CONVAR_GLOBAL_SLOT,
            &string_read) != KEEL_RESULT_OK ||
        !string_read.value.string_value ||
        std::strcmp(string_read.value.string_value, "keels2-changed") != 0)
    {
        return false;
    }

    KeelConVarSpec invalid = integer;
    invalid.size = 0;
    KeelConVarHandle invalid_handle = 999;
    if (g_convars->create(g_plugin, &invalid, &invalid_handle) != KEEL_RESULT_INVALID_ARGUMENT ||
        invalid_handle != 0)
    {
        return false;
    }
    invalid = integer;
    invalid.flags = 1ull << 63;
    if (g_convars->create(g_plugin, &invalid, &invalid_handle) != KEEL_RESULT_INVALID_ARGUMENT)
    {
        return false;
    }
    invalid = integer;
    invalid.minimum_value = IntValue(12);
    if (g_convars->create(g_plugin, &invalid, &invalid_handle) != KEEL_RESULT_INVALID_ARGUMENT)
    {
        return false;
    }
    invalid = floating;
    invalid.default_value = FloatValue(std::numeric_limits<float>::quiet_NaN());
    if (g_convars->create(g_plugin, &invalid, &invalid_handle) != KEEL_RESULT_INVALID_ARGUMENT)
    {
        return false;
    }

    KeelConVarHandle existing{};
    if (g_convars->find(
            g_plugin,
            "sv_keels2_existing",
            KEELS2_CONVAR_FLOAT32,
            &existing) != KEEL_RESULT_INCOMPATIBLE || existing != 0 ||
        g_convars->find(
            g_plugin,
            "sv_keels2_existing",
            KEELS2_CONVAR_INT32,
            &existing) != KEEL_RESULT_OK || existing == 0)
    {
        return false;
    }
    KeelConVarValue existing_value{};
    existing_value.size = sizeof(existing_value);
    KeelConVarInfo existing_info{};
    existing_info.size = sizeof(existing_info);
    if (g_convars->read(
            g_plugin,
            existing,
            KEELS2_CONVAR_GLOBAL_SLOT,
            &existing_value) != KEEL_RESULT_OK ||
        existing_value.type != KEELS2_CONVAR_INT32 ||
        existing_value.value.int32_value != 42 ||
        g_convars->describe(g_plugin, existing, &existing_info) != KEEL_RESULT_OK ||
        existing_info.type != KEELS2_CONVAR_INT32 ||
        g_convars->release(g_plugin, existing) != KEEL_RESULT_OK)
    {
        return false;
    }

    KeelConVarInfo integer_info{};
    integer_info.size = sizeof(integer_info);
    if (g_convars->describe(g_plugin, g_integer, &integer_info) != KEEL_RESULT_OK ||
        integer_info.type != KEELS2_CONVAR_INT32 ||
        !integer_info.name || std::strcmp(integer_info.name, "keels2_test_int") != 0 ||
        integer_info.default_value.value.int32_value != 7 ||
        integer_info.has_minimum != KEEL_TRUE ||
        integer_info.minimum_value.value.int32_value != 1 ||
        integer_info.has_maximum != KEEL_TRUE ||
        integer_info.maximum_value.value.int32_value != 11 ||
        (integer_info.flags & (KEELS2_CVAR_FLAG_NOTIFY | KEELS2_CVAR_FLAG_RELEASE)) !=
            (KEELS2_CVAR_FLAG_NOTIFY | KEELS2_CVAR_FLAG_RELEASE))
    {
        return false;
    }

    const KeelConVarValue staged = IntValue(8);
    KeelConVarValue staged_read{};
    staged_read.size = sizeof(staged_read);
    if (g_convars->queue_set(
            g_plugin,
            g_integer,
            KEELS2_CONVAR_GLOBAL_SLOT,
            &staged) != KEEL_RESULT_OK ||
        g_convars->read(
            g_plugin,
            g_integer,
            KEELS2_CONVAR_GLOBAL_SLOT,
            &staged_read) != KEEL_RESULT_OK ||
        staged_read.value.int32_value != 8 ||
        g_callback_count.load(std::memory_order_acquire) != 0)
    {
        return false;
    }
    return true;
}

}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query(
    const KeelHostQuery* query,
    KeelPluginInfo* info)
{
    if (!query || query->size != sizeof(KeelHostQuery) ||
        query->abi_version != KEELS2_PLUGIN_ABI_VERSION ||
        !info || info->size != sizeof(KeelPluginInfo))
    {
        return KEEL_FALSE;
    }
    *info = {
        sizeof(KeelPluginInfo),
        KEELS2_PLUGIN_ABI_VERSION,
        "ConVar Service Test",
        "KeelS2 Project",
        "0.5D",
        "Brokered ConVar integration fixture"
    };
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load(
    const KeelHostApi* host,
    KeelPluginHandle plugin)
{
    g_host = host;
    g_plugin = plugin;
    g_integer = 0;
    g_callback_count.store(0, std::memory_order_release);
    g_invalid_count.store(0, std::memory_order_release);
    g_busy_count.store(0, std::memory_order_release);
    g_block_armed.store(false, std::memory_order_release);
    g_block_entered.store(false, std::memory_order_release);
    g_block_release.store(false, std::memory_order_release);
    g_loaded = false;
    if (!host || host->size != sizeof(KeelHostApi) ||
        host->abi_version != KEELS2_PLUGIN_ABI_VERSION || !host->query_service || !host->log)
    {
        return KEEL_FALSE;
    }
    const void* service = reinterpret_cast<const void*>(1);
    if (host->query_service(
            plugin,
            KEELS2_CONVAR_SERVICE_NAME,
            KEELS2_CONVAR_API_VERSION + 1,
            &service) != KEEL_RESULT_INCOMPATIBLE || service)
    {
        return KEEL_FALSE;
    }
    if (host->query_service(
            plugin,
            KEELS2_CONVAR_SERVICE_NAME,
            KEELS2_CONVAR_API_VERSION,
            &service) != KEEL_RESULT_OK || !service)
    {
        return KEEL_FALSE;
    }
    g_convars = static_cast<const KeelConVarApi*>(service);
    const bool service_invalid = g_convars->size != sizeof(KeelConVarApi) ||
        g_convars->api_version != KEELS2_CONVAR_API_VERSION ||
        !g_convars->create || !g_convars->find || !g_convars->release ||
        !g_convars->read || !g_convars->queue_set || !g_convars->describe;
    if (service_invalid || !CreateFixtures())
    {
        if (!std::getenv("KEELS2_TEST_CONVAR_INCOMPATIBLE"))
        {
            Log(KEEL_LOG_ERROR, "ConVar broker load validation failed");
        }
        return KEEL_FALSE;
    }
    if (std::getenv("KEELS2_TEST_CONVAR_FAIL_LOAD"))
    {
        Log(KEEL_LOG_INFO, "rejecting load after staged ConVar creation");
        return KEEL_FALSE;
    }
    g_loaded = true;
    Log(KEEL_LOG_INFO, "staged ConVar contract passed without callback reentry");
    return KEEL_TRUE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle)
{
    if (g_loaded)
    {
        KeelConVarValue value{};
        value.size = sizeof(value);
        const KeelResult unavailable = g_convars
            ? g_convars->read(
                g_plugin,
                g_integer,
                KEELS2_CONVAR_GLOBAL_SLOT,
                &value)
            : KEEL_RESULT_NOT_READY;
        if (unavailable != KEEL_RESULT_NOT_READY)
        {
            g_invalid_count.fetch_add(1, std::memory_order_acq_rel);
        }
        g_unload_count.fetch_add(1, std::memory_order_acq_rel);
        Log(KEEL_LOG_INFO, "unloaded after ConVar callback drain");
    }
    g_loaded = false;
    g_convars = nullptr;
    g_host = nullptr;
    g_plugin = 0;
    g_integer = 0;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_ConVarArmBlock()
{
    g_block_release.store(false, std::memory_order_release);
    g_block_entered.store(false, std::memory_order_release);
    g_block_armed.store(true, std::memory_order_release);
}

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_ConVarBlockEntered()
{
    return g_block_entered.load(std::memory_order_acquire) ? KEEL_TRUE : KEEL_FALSE;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_ConVarReleaseBlock()
{
    g_block_release.store(true, std::memory_order_release);
    g_block_release.notify_all();
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_ConVarCallbackCount()
{
    return g_callback_count.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_ConVarInvalidCount()
{
    return g_invalid_count.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_ConVarBusyCount()
{
    return g_busy_count.load(std::memory_order_acquire);
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_ConVarUnloadCount()
{
    return g_unload_count.load(std::memory_order_acquire);
}
