#include "host.h"
#include "convar_service.h"
#include "keelhook_service.h"
#include "lifecycle_service.h"
#include "plugin_service.h"
#include "schema_entity_service.h"
#include "source2_callbacks_service.h"

#include <keels2/platform/console.h>
#include <keels2/platform/diagnostic_trace.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#define KEELS2_HOST_EXPORT __declspec(dllexport)
#else
#define KEELS2_HOST_EXPORT __attribute__((visibility("default")))
#endif

namespace keels2::host
{

namespace
{

std::atomic<std::uint32_t> g_dispatch_entries{};

void ReleaseDispatchEntry() noexcept
{
    if (g_dispatch_entries.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        g_dispatch_entries.notify_all();
    }
}

void WaitForDispatchEntries() noexcept
{
    std::uint32_t entries = g_dispatch_entries.load(std::memory_order_acquire);
    while (entries != 0)
    {
        g_dispatch_entries.wait(entries, std::memory_order_acquire);
        entries = g_dispatch_entries.load(std::memory_order_acquire);
    }
}

}

Host& Host::Instance()
{
    static Host instance;
    return instance;
}

Host::~Host() = default;

std::uint32_t Host::Start(const KeelHostStartInfo& info)
{
    std::unique_lock lifecycle_lock(lifecycle_mutex_);
    std::unique_lock state_lock(state_mutex_);
    if (state_ == HostState::running)
    {
        return KEELS2_HOST_START_RUNNING;
    }
    if (state_ != HostState::stopped)
    {
        Write(KEEL_LOG_ERROR, "host start requested during another lifecycle transition");
        return state_ == HostState::stopping
            ? KEELS2_HOST_START_RETAINED
            : KEELS2_HOST_START_FAILED;
    }
    if (info.size != sizeof(KeelHostStartInfo) ||
        info.abi_version != KEELS2_HOST_ABI_VERSION)
    {
        Write(KEEL_LOG_ERROR, "host ABI version is incompatible");
        return KEELS2_HOST_START_FAILED;
    }
    if (!info.engine_factory || !info.server_factory || !info.bootstrap_directory ||
        !info.game || !info.platform || !info.compatibility)
    {
        Write(KEEL_LOG_ERROR, "host start information is incomplete");
        return KEELS2_HOST_START_FAILED;
    }
    if (std::strcmp(info.platform, kPlatformName) != 0)
    {
        Write(KEEL_LOG_ERROR, "host platform does not match this binary");
        return KEELS2_HOST_START_FAILED;
    }
    if (info.compatibility->size != sizeof(KeelHostCompatibilityInfo) ||
        !info.compatibility->profile || !info.compatibility->profile[0] ||
        !info.compatibility->game_version || !info.compatibility->game_version[0])
    {
        Write(KEEL_LOG_ERROR, "host compatibility information is incomplete");
        return KEELS2_HOST_START_FAILED;
    }

    state_ = HostState::starting;
    try
    {
        game_ = info.game;
        platform_ = info.platform;
        game_version_ = info.compatibility->game_version;
        compatibility_profile_ = info.compatibility->profile;
        bootstrap_directory_ = std::filesystem::path(info.bootstrap_directory);
        adapter_ = CreateGameAdapter();
        if (!adapter_ || game_ != adapter_->Name())
        {
            Write(KEEL_LOG_ERROR, "selected game adapter does not match host start information");
            state_ = HostState::stopping;
            if (ReleaseResources(state_lock))
            {
                state_ = HostState::stopped;
                return KEELS2_HOST_START_FAILED;
            }
            return KEELS2_HOST_START_RETAINED;
        }

        std::string error;
        if (!adapter_->Start(
                info.engine_factory,
                info.server_factory,
                *info.compatibility,
                error))
        {
            Write(KEEL_LOG_ERROR, "game adapter failed: " + error);
            state_ = HostState::stopping;
            if (ReleaseResources(state_lock))
            {
                state_ = HostState::stopped;
                return KEELS2_HOST_START_FAILED;
            }
            return KEELS2_HOST_START_RETAINED;
        }

        source2_api_v1_ = {
            sizeof(KeelSource2ApiV1),
            KEELS2_SOURCE2_API_VERSION_1,
            &ApiQuerySource2Interface
        };
        source2_api_ = {
            sizeof(KeelSource2Api),
            KEELS2_SOURCE2_API_VERSION,
            &ApiQuerySource2Interface,
            &ApiQuerySource2NamedInterface
        };
        source2_authoring_api_ = {
            sizeof(KeelSource2AuthoringApi),
            KEELS2_SOURCE2_AUTHORING_API_VERSION,
            &ApiRegisterSource2Command,
            &ApiUnregisterSource2Command,
            &ApiCreateSource2ConVar,
            &ApiFindSource2ConVar,
            &ApiReleaseSource2ConVar
        };

        api_.size = sizeof(api_);
        api_.abi_version = KEELS2_PLUGIN_ABI_VERSION;
        api_.log = &ApiLog;
        api_.register_command = &ApiRegisterCommand;
        api_.unregister_command = &ApiUnregisterCommand;
        api_.query_service = &ApiQueryService;

        keelhook_ = std::make_unique<KeelHookService>(*this);
#if defined(_WIN32)
        const char* host_name = "keels2_host.dll";
#else
        const char* host_name = "libkeels2_host.so";
#endif
        keelhook_->Authorize(0, bootstrap_directory_ / host_name, true);
        source2_callbacks_ = std::make_unique<Source2CallbacksService>(
            *this,
            *adapter_,
            *keelhook_);

        if (!RegisterCoreCommand())
        {
            Write(KEEL_LOG_ERROR, "core command registration failed");
            state_ = HostState::stopping;
            if (ReleaseResources(state_lock))
            {
                state_ = HostState::stopped;
                return KEELS2_HOST_START_FAILED;
            }
            return KEELS2_HOST_START_RETAINED;
        }

        accepting_resources_ = true;
        plugin_directory_ =
            bootstrap_directory_.parent_path().parent_path() / "plugins" / platform_;
        LoadPlugins(plugin_directory_, state_lock);
        state_ = HostState::running;
        dispatch_open_.store(true, std::memory_order_release);
        Write(KEEL_LOG_INFO, "host started for " + game_);
        return KEELS2_HOST_START_RUNNING;
    }
    catch (...)
    {
        accepting_resources_ = false;
        dispatch_open_.store(false, std::memory_order_release);
        state_ = HostState::stopping;
        state_lock.unlock();
        lifecycle_lock.unlock();
        WaitForDispatchEntries();
        lifecycle_lock.lock();
        state_lock.lock();
        if (ReleaseResources(state_lock))
        {
            state_ = HostState::stopped;
        }
        const std::uint32_t result = state_ == HostState::stopped
            ? KEELS2_HOST_START_FAILED
            : KEELS2_HOST_START_RETAINED;
        state_lock.unlock();
        Write(KEEL_LOG_ERROR, "host startup failed with an internal exception");
        return result;
    }
}

bool Host::CompleteStartup()
{
    std::unique_lock lifecycle_lock(lifecycle_mutex_);
    std::unique_lock state_lock(state_mutex_);
    if (state_ != HostState::running || !adapter_)
    {
        return false;
    }
    std::string error;
    if (!adapter_->CompleteStartup(error))
    {
        Write(
            KEEL_LOG_ERROR,
            "game adapter completion failed: " +
                (error.empty() ? std::string("unknown failure") : error));
        return false;
    }
    return true;
}

bool Host::Stop()
{
    std::unique_lock lifecycle_lock(lifecycle_mutex_);
    std::unique_lock state_lock(state_mutex_);
    if (state_ == HostState::stopped)
    {
        return true;
    }
    if (state_ == HostState::running)
    {
        state_ = HostState::stopping;
        accepting_resources_ = false;
        dispatch_open_.store(false, std::memory_order_release);
        state_lock.unlock();
        lifecycle_lock.unlock();
        WaitForDispatchEntries();
        lifecycle_lock.lock();
        state_lock.lock();
    }
    else if (state_ != HostState::stopping)
    {
        return false;
    }
    WriteShutdownTrace("host stop begin");
    if (!ReleaseResources(state_lock))
    {
        WriteShutdownTrace("host stop retained");
        return false;
    }
    state_ = HostState::stopped;
    state_lock.unlock();
    lifecycle_lock.unlock();
    WriteShutdownTrace("host stopped");
    Write(KEEL_LOG_INFO, "host stopped");
    return true;
}

bool Host::CommandDispatchOpen() const noexcept
{
    return dispatch_open_.load(std::memory_order_acquire);
}

bool Host::ReleaseResources(std::unique_lock<std::recursive_mutex>& state_lock)
{
    WriteShutdownTrace("host resource release begin");
    accepting_resources_ = false;
    const bool plugin_transition_active =
        std::any_of(plugins_.begin(), plugins_.end(), [](const auto& plugin) {
            return plugin->transitioning;
        });
    if (plugin_transition_active)
    {
        WriteShutdownTrace("plugin transition is still active");
        if (!plugin_transition_failure_reported_)
        {
            Write(KEEL_LOG_ERROR, "host cleanup is waiting for an active plugin transition");
            plugin_transition_failure_reported_ = true;
        }
        return false;
    }
    plugin_transition_failure_reported_ = false;
    for (auto& plugin : plugins_)
    {
        plugin->accepting_resources = false;
    }
    for (auto& [handle, command] : commands_)
    {
        static_cast<void>(handle);
        command->enabled.store(false, std::memory_order_release);
    }
    state_lock.unlock();
    WriteShutdownTrace("plugin service shutdown begin");
    const bool plugin_events_stopped = !plugin_service_ || plugin_service_->Shutdown();
    if (!plugin_events_stopped)
    {
        state_lock.lock();
        WriteShutdownTrace("plugin event dispatch quiescence failed");
        if (!plugin_service_failure_reported_)
        {
            Write(KEEL_LOG_ERROR, "plugin event callbacks are still active during shutdown");
            plugin_service_failure_reported_ = true;
        }
        return false;
    }
    WriteShutdownTrace("plugin service shutdown complete");
    plugin_service_failure_reported_ = false;
    WriteShutdownTrace("ConVar service shutdown begin");
    const bool convars_stopped = !convars_ || convars_->Shutdown();
    if (!convars_stopped)
    {
        state_lock.lock();
        WriteShutdownTrace("ConVar dispatch quiescence failed");
        if (!convar_failure_reported_)
        {
            Write(KEEL_LOG_ERROR, "ConVar callbacks are still active during shutdown");
            convar_failure_reported_ = true;
        }
        return false;
    }
    WriteShutdownTrace("ConVar service shutdown complete");
    convar_failure_reported_ = false;
    WriteShutdownTrace("schema and entity service shutdown begin");
    const bool schema_entities_stopped =
        !schema_entities_ || schema_entities_->Shutdown();
    if (!schema_entities_stopped)
    {
        state_lock.lock();
        WriteShutdownTrace("schema and entity service shutdown failed");
        return false;
    }
    WriteShutdownTrace("schema and entity service shutdown complete");
    WriteShutdownTrace("lifecycle service shutdown begin");
    const bool lifecycle_stopped = !lifecycle_ || lifecycle_->Shutdown();
    if (!lifecycle_stopped)
    {
        state_lock.lock();
        WriteShutdownTrace("lifecycle dispatch quiescence failed");
        if (!lifecycle_failure_reported_)
        {
            Write(KEEL_LOG_ERROR, "lifecycle callbacks are still active during shutdown");
            lifecycle_failure_reported_ = true;
        }
        return false;
    }
    WriteShutdownTrace("lifecycle service shutdown complete");
    lifecycle_failure_reported_ = false;
    WriteShutdownTrace("Source 2 callback service shutdown begin");
    const bool source2_callbacks_stopped =
        !source2_callbacks_ || source2_callbacks_->Shutdown();
    if (!source2_callbacks_stopped)
    {
        state_lock.lock();
        WriteShutdownTrace("Source 2 callback dispatch quiescence failed");
        if (!source2_callbacks_failure_reported_)
        {
            Write(KEEL_LOG_ERROR, "Source 2 callbacks are still active during shutdown");
            source2_callbacks_failure_reported_ = true;
        }
        return false;
    }
    WriteShutdownTrace("Source 2 callback service shutdown complete");
    source2_callbacks_failure_reported_ = false;
    WriteShutdownTrace("keelhook shutdown begin");
    const bool hooks_restored = !keelhook_ || keelhook_->Shutdown();
    state_lock.lock();
    if (!hooks_restored)
    {
        WriteShutdownTrace("keelhook restoration failed");
        if (!cleanup_failure_reported_)
        {
            Write(KEEL_LOG_ERROR, "KeelHook could not restore every physical target during shutdown");
            cleanup_failure_reported_ = true;
        }
        return false;
    }
    WriteShutdownTrace("keelhook restoration completed");
    cleanup_failure_reported_ = false;
    WriteShutdownTrace("adapter command release begin");
    if (adapter_)
    {
        for (auto& [handle, command] : commands_)
        {
            static_cast<void>(handle);
            adapter_->UnregisterCommand(command->game_handle);
        }
    }
    WriteShutdownTrace("adapter command release complete");
    WaitForDispatchEntries();
    WriteShutdownTrace("host dispatch drain complete");
    commands_.clear();
    retired_commands_.clear();
    WriteShutdownTrace("host command records cleared");

    WriteShutdownTrace("plugin unload loop begin");
    for (auto iterator = load_order_.rbegin(); iterator != load_order_.rend(); ++iterator)
    {
        PluginRecord* plugin = PluginByHandle(*iterator);
        if (!plugin || !plugin->library.IsOpen() || !plugin->unload)
        {
            continue;
        }
        plugin->accepting_resources = false;
        const std::string display_id = PluginDisplayId(plugin);
        const std::string name = plugin->name;
        bool callback_completed = true;
        state_lock.unlock();
        try
        {
            plugin->unload(plugin->handle);
        }
        catch (...)
        {
            callback_completed = false;
            Write(KEEL_LOG_ERROR, "plugin threw during unload: " + plugin->name);
        }
        state_lock.lock();
        WriteShutdownTrace(
            callback_completed
                ? "plugin unload callback completed"
                : "plugin unload callback threw",
            name
        );
        plugin->load = nullptr;
        plugin->unload = nullptr;
        plugin->library.Close();
        WriteShutdownTrace("plugin module released", name);
        Write(KEEL_LOG_INFO, "plugin unloaded: [" + display_id + "] " + name);
    }
    WriteShutdownTrace("plugin unload loop complete");

    load_order_.clear();
    WriteShutdownTrace("plugin load order cleared");
    plugins_.clear();
    WriteShutdownTrace("plugin records cleared");
    plugin_service_.reset();
    WriteShutdownTrace("plugin service released");
    source2_callbacks_.reset();
    WriteShutdownTrace("Source 2 callback service released");
    convars_.reset();
    WriteShutdownTrace("ConVar service released");
    schema_entities_.reset();
    WriteShutdownTrace("schema and entity service released");
    lifecycle_.reset();
    WriteShutdownTrace("lifecycle service released");
    keelhook_.reset();
    WriteShutdownTrace("keelhook service released");
    if (adapter_)
    {
        WriteShutdownTrace("game adapter stop begin");
        adapter_->Stop();
        WriteShutdownTrace("game adapter stop complete");
    }
    adapter_.reset();
    WriteShutdownTrace("game adapter released");
    source2_api_v1_ = {};
    source2_api_ = {};
    source2_authoring_api_ = {};
    api_ = {};
    WriteShutdownTrace("host APIs cleared");
    next_plugin_ = 1;
    next_command_ = 1;
    game_.clear();
    platform_.clear();
    game_version_.clear();
    compatibility_profile_.clear();
    bootstrap_directory_.clear();
    plugin_directory_.clear();
    WriteShutdownTrace("host state cleared");
    WriteShutdownTrace("host resource release complete");
    return true;
}

bool Host::RegisterCoreCommand()
{
    if (next_command_ == 0)
    {
        return false;
    }
    auto resource = std::make_unique<CommandRecord>();
    resource->handle = next_command_++;
    resource->name = "keel";
    resource->description = "KeelS2 command menu";
    resource->callback = &CoreCommand;
    return RegisterCommandRecord(std::move(resource), 0);
}

void Host::ApiLog(KeelPluginHandle plugin, KeelLogLevel level, const char* message)
{
    Instance().PluginLog(plugin, level, message);
}

KeelResult Host::ApiRegisterCommand(
    KeelPluginHandle plugin,
    const KeelCommandSpec* spec,
    KeelCommandHandle* command)
{
    try
    {
        return Instance().RegisterCommand(plugin, spec, command);
    }
    catch (...)
    {
        Instance().Write(KEEL_LOG_ERROR, "exception while registering a command");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Host::ApiUnregisterCommand(
    KeelPluginHandle plugin,
    KeelCommandHandle command)
{
    try
    {
        return Instance().UnregisterCommand(plugin, command);
    }
    catch (...)
    {
        Instance().Write(KEEL_LOG_ERROR, "exception while unregistering a command");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Host::ApiRegisterSource2Command(
    KeelPluginHandle plugin,
    const KeelSource2CommandSpec* spec,
    KeelCommandHandle* command)
{
    try
    {
        return Instance().RegisterSource2Command(plugin, spec, command);
    }
    catch (...)
    {
        Instance().Write(KEEL_LOG_ERROR, "exception while registering a Source 2 command");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Host::ApiUnregisterSource2Command(
    KeelPluginHandle plugin,
    KeelCommandHandle command)
{
    return ApiUnregisterCommand(plugin, command);
}

KeelResult Host::ApiCreateSource2ConVar(
    KeelPluginHandle plugin,
    const KeelConVarSpec* spec,
    KeelSource2ConVarChangeCallback callback,
    void* user_data,
    KeelConVarHandle* convar,
    void** native_convar)
{
    try
    {
        Host& host = Instance();
        return host.convars_
            ? host.convars_->CreateNative(
                plugin,
                spec,
                callback,
                user_data,
                convar,
                native_convar)
            : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        Instance().Write(KEEL_LOG_ERROR, "exception while creating a Source 2 ConVar");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Host::ApiFindSource2ConVar(
    KeelPluginHandle plugin,
    const char* name,
    KeelConVarType expected_type,
    KeelConVarHandle* convar,
    void** native_convar)
{
    try
    {
        Host& host = Instance();
        return host.convars_
            ? host.convars_->FindNative(
                plugin,
                name,
                expected_type,
                convar,
                native_convar)
            : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        Instance().Write(KEEL_LOG_ERROR, "exception while finding a Source 2 ConVar");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Host::ApiReleaseSource2ConVar(
    KeelPluginHandle plugin,
    KeelConVarHandle convar)
{
    try
    {
        Host& host = Instance();
        return host.convars_
            ? host.convars_->ReleaseNative(plugin, convar)
            : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        Instance().Write(KEEL_LOG_ERROR, "exception while releasing a Source 2 ConVar");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Host::ApiQueryService(
    KeelPluginHandle plugin,
    const char* name,
    std::uint32_t version,
    const void** service)
{
    try
    {
        return Instance().QueryService(plugin, name, version, service);
    }
    catch (...)
    {
        Instance().Write(KEEL_LOG_ERROR, "exception while querying a host service");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Host::ApiQuerySource2Interface(
    KeelPluginHandle plugin,
    KeelSource2Capability capability,
    KeelSource2InterfaceInfo* info)
{
    try
    {
        return Instance().QuerySource2Interface(plugin, capability, info);
    }
    catch (...)
    {
        Instance().Write(KEEL_LOG_ERROR, "exception while querying a Source 2 interface");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Host::ApiQuerySource2NamedInterface(
    KeelPluginHandle plugin,
    KeelSource2Factory factory,
    const char* interface_name,
    KeelSource2InterfaceInfo* info)
{
    try
    {
        return Instance().QuerySource2NamedInterface(
            plugin,
            factory,
            interface_name,
            info);
    }
    catch (...)
    {
        Instance().Write(KEEL_LOG_ERROR, "exception while querying a named Source 2 interface");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Host::QueryService(
    KeelPluginHandle plugin,
    const char* name,
    std::uint32_t version,
    const void** service)
{
    std::scoped_lock lock(state_mutex_);
    if (!service || !name)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *service = nullptr;
    PluginRecord* owner = PluginByHandle(plugin);
    if (!accepting_resources_ || !owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (std::strcmp(name, KEELS2_SOURCE2_SERVICE_NAME) == 0)
    {
        if (version == KEELS2_SOURCE2_API_VERSION_1)
        {
            if (!source2_api_v1_.query_interface)
            {
                return KEEL_RESULT_NOT_READY;
            }
            *service = &source2_api_v1_;
            return KEEL_RESULT_OK;
        }
        if (version == KEELS2_SOURCE2_API_VERSION)
        {
            if (!source2_api_.query_interface || !source2_api_.query_named_interface)
            {
                return KEEL_RESULT_NOT_READY;
            }
            *service = &source2_api_;
            return KEEL_RESULT_OK;
        }
        return KEEL_RESULT_INCOMPATIBLE;
    }
    if (std::strcmp(name, KEELS2_SOURCE2_AUTHORING_SERVICE_NAME) == 0)
    {
        if (version != KEELS2_SOURCE2_AUTHORING_API_VERSION)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (!convars_)
        {
            convars_ = std::make_unique<ConVarService>(*this, *adapter_);
        }
        *service = &source2_authoring_api_;
        return KEEL_RESULT_OK;
    }
    if (std::strcmp(name, KEELS2_SOURCE2_CALLBACKS_SERVICE_NAME) == 0)
    {
        if (version != KEELS2_SOURCE2_CALLBACKS_API_VERSION)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (!source2_callbacks_)
        {
            if (!keelhook_)
            {
                keelhook_ = std::make_unique<KeelHookService>(*this);
            }
#if defined(_WIN32)
            const char* host_name = "keels2_host.dll";
#else
            const char* host_name = "libkeels2_host.so";
#endif
            keelhook_->Authorize(0, bootstrap_directory_ / host_name, true);
            source2_callbacks_ = std::make_unique<Source2CallbacksService>(
                *this,
                *adapter_,
                *keelhook_);
        }
        *service = &source2_callbacks_->Api();
        return KEEL_RESULT_OK;
    }
    if (std::strcmp(name, KEELS2_LIFECYCLE_SERVICE_NAME) == 0)
    {
        if (version != KEELS2_LIFECYCLE_API_VERSION)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (!lifecycle_)
        {
            if (!keelhook_)
            {
                keelhook_ = std::make_unique<KeelHookService>(*this);
            }
#if defined(_WIN32)
            const char* host_name = "keels2_host.dll";
#else
            const char* host_name = "libkeels2_host.so";
#endif
            keelhook_->Authorize(0, bootstrap_directory_ / host_name, true);
            lifecycle_ = std::make_unique<LifecycleService>(*this, *adapter_, *keelhook_);
        }
        *service = &lifecycle_->Api();
        return KEEL_RESULT_OK;
    }
    if (std::strcmp(name, KEELS2_CONVAR_SERVICE_NAME) == 0)
    {
        if (version != KEELS2_CONVAR_API_VERSION)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (!convars_)
        {
            convars_ = std::make_unique<ConVarService>(*this, *adapter_);
        }
        *service = &convars_->Api();
        return KEEL_RESULT_OK;
    }
    if (std::strcmp(name, KEELS2_PLUGINS_SERVICE_NAME) == 0)
    {
        if (version != KEELS2_PLUGINS_API_VERSION)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (!plugin_service_)
        {
            plugin_service_ = std::make_unique<PluginService>(*this);
        }
        *service = &plugin_service_->Api();
        return KEEL_RESULT_OK;
    }
    if (std::strcmp(name, KEELS2_SCHEMA_SERVICE_NAME) == 0 ||
        std::strcmp(name, KEELS2_ENTITIES_SERVICE_NAME) == 0)
    {
        const bool schema = std::strcmp(name, KEELS2_SCHEMA_SERVICE_NAME) == 0;
        if ((schema && version != KEELS2_SCHEMA_API_VERSION) ||
            (!schema && version != KEELS2_ENTITIES_API_VERSION))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (!schema_entities_)
        {
            schema_entities_ = std::make_unique<SchemaEntityService>(*this, *adapter_);
        }
        *service = schema
            ? static_cast<const void*>(&schema_entities_->SchemaApi())
            : static_cast<const void*>(&schema_entities_->EntitiesApi());
        return KEEL_RESULT_OK;
    }
    if (std::strcmp(name, KEELHOOK_SERVICE_NAME) != 0)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (version != KEELHOOK_API_VERSION)
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    if (!keelhook_)
    {
        keelhook_ = std::make_unique<KeelHookService>(*this);
    }
    keelhook_->Authorize(
        plugin,
        owner->path,
        owner->state == PluginState::loaded && !owner->loading);
    *service = &keelhook_->Api();
    return KEEL_RESULT_OK;
}

KeelResult Host::QuerySource2Interface(
    KeelPluginHandle plugin,
    KeelSource2Capability capability,
    KeelSource2InterfaceInfo* info)
{
    std::scoped_lock lock(state_mutex_);
    if (!info)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    const std::uint32_t size = info->size;
    *info = {};
    info->size = size;
    if (size != sizeof(KeelSource2InterfaceInfo))
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    PluginRecord* owner = PluginByHandle(plugin);
    if (!accepting_resources_ || !owner || !owner->accepting_resources || !adapter_)
    {
        return KEEL_RESULT_NOT_READY;
    }
    return adapter_->QueryInterface(capability, *info);
}

KeelResult Host::QuerySource2NamedInterface(
    KeelPluginHandle plugin,
    KeelSource2Factory factory,
    const char* interface_name,
    KeelSource2InterfaceInfo* info)
{
    std::scoped_lock lock(state_mutex_);
    if (!info || !interface_name)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    const std::uint32_t size = info->size;
    *info = {};
    info->size = size;
    if (size != sizeof(KeelSource2InterfaceInfo))
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    if ((factory != KEELS2_SOURCE2_FACTORY_ENGINE &&
            factory != KEELS2_SOURCE2_FACTORY_SERVER) || !interface_name[0])
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    PluginRecord* owner = PluginByHandle(plugin);
    if (!accepting_resources_ || !owner || !owner->accepting_resources || !adapter_)
    {
        return KEEL_RESULT_NOT_READY;
    }
    return adapter_->QueryNamedInterface(factory, interface_name, *info);
}

void Host::DispatchCommand(const GameCommandInvocation& game_invocation, void* user_data)
{
    Host& host = Instance();
    auto* command = static_cast<CommandRecord*>(user_data);
    std::unique_lock lifecycle_lock(host.lifecycle_mutex_, std::defer_lock);
    if (command && command->owner == 0)
    {
        lifecycle_lock.lock();
    }
    std::unique_lock state_lock(host.state_mutex_);
    if (!host.dispatch_open_.load(std::memory_order_acquire))
    {
        return;
    }

    if (!command || !command->enabled.load(std::memory_order_acquire))
    {
        return;
    }

    try
    {
        if (command->native_callback)
        {
            if (game_invocation.context && game_invocation.command)
            {
                command->native_callback(
                    game_invocation.context,
                    game_invocation.command,
                    command->user_data);
            }
        }
        else
        {
            const std::uint32_t argument_count =
                game_invocation.argument_count > 0 ? game_invocation.argument_count - 1 : 0;
            const char* const* arguments =
                argument_count != 0 && game_invocation.arguments
                    ? game_invocation.arguments + 1
                    : nullptr;
            const KeelCommandInvocation invocation{
                sizeof(KeelCommandInvocation),
                argument_count,
                command->name.c_str(),
                arguments
            };
            if (command->owner == 0)
            {
                host.DispatchCoreCommand(invocation, state_lock);
            }
            else
            {
                command->callback(&invocation, command->user_data);
            }
        }
    }
    catch (...)
    {
        if (command->owner == 0)
        {
            host.Write(KEEL_LOG_ERROR, "core command failed with an internal exception");
        }
        else
        {
            host.Write(KEEL_LOG_ERROR, "plugin threw from command: " + command->name);
        }
    }
}

void Host::CoreCommand(const KeelCommandInvocation* invocation, void*)
{
    if (!invocation || invocation->size != sizeof(KeelCommandInvocation))
    {
        Instance().Write(KEEL_LOG_ERROR, "core command received an invalid invocation");
        return;
    }
    Host& host = Instance();
    std::unique_lock lifecycle_lock(host.lifecycle_mutex_);
    std::unique_lock state_lock(host.state_mutex_);
    host.DispatchCoreCommand(*invocation, state_lock);
}

void Host::PluginLog(KeelPluginHandle plugin, KeelLogLevel level, const char* message)
{
    if (!message)
    {
        Write(KEEL_LOG_ERROR, "plugin log rejected: message is null");
        return;
    }

    if (level != KEEL_LOG_INFO && level != KEEL_LOG_WARNING && level != KEEL_LOG_ERROR)
    {
        Write(KEEL_LOG_ERROR, "plugin log rejected: invalid log level");
        return;
    }

    std::string name;
    {
        std::scoped_lock lock(state_mutex_);
        const PluginRecord* owner = PluginByHandle(plugin);
        if (!owner)
        {
            Write(KEEL_LOG_ERROR, "plugin log rejected: invalid plugin handle");
            return;
        }
        name = owner->name.empty() ? owner->path.filename().string() : owner->name;
    }

    std::string line = "[" + name + "] ";
    if (level == KEEL_LOG_WARNING)
    {
        line += "WARNING: ";
    }
    else if (level == KEEL_LOG_ERROR)
    {
        line += "ERROR: ";
    }
    line += message;
    WriteLine(line);
}

KeelResult Host::RegisterCommand(
    KeelPluginHandle plugin,
    const KeelCommandSpec* spec,
    KeelCommandHandle* output)
{
    std::scoped_lock lock(state_mutex_);
    if (!accepting_resources_ || !adapter_)
    {
        return KEEL_RESULT_NOT_READY;
    }

    PluginRecord* owner = PluginByHandle(plugin);
    if (!spec || spec->size != sizeof(KeelCommandSpec) || !output ||
        !spec->name || !spec->callback || !owner)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (!owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (next_command_ == 0)
    {
        Write(KEEL_LOG_ERROR, "command handle space is exhausted");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    if (EqualInsensitive(spec->name, "keel"))
    {
        Write(
            KEEL_LOG_ERROR,
            "plugin \"" + owner->name + "\" cannot register reserved command \"keel\""
        );
        return KEEL_RESULT_RESERVED_NAME;
    }
    if (!ValidCommandName(spec->name) ||
        !ValidMetadataText(spec->description, 255, true))
    {
        Write(KEEL_LOG_ERROR, "plugin \"" + owner->name + "\" supplied an invalid command definition");
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (std::any_of(commands_.begin(), commands_.end(), [spec](const auto& entry) {
            return EqualInsensitive(entry.second->name, spec->name);
        }))
    {
        Write(
            KEEL_LOG_ERROR,
            "plugin \"" + owner->name + "\" cannot register duplicate command \"" +
                std::string(spec->name) + "\""
        );
        return KEEL_RESULT_ALREADY_EXISTS;
    }

    auto resource = std::make_unique<CommandRecord>();
    resource->handle = next_command_++;
    resource->owner = plugin;
    resource->name = spec->name;
    resource->description = spec->description ? spec->description : "";
    resource->callback = spec->callback;
    resource->user_data = spec->user_data;
    resource->enabled.store(!owner->loading && owner->state == PluginState::loaded);
    const KeelCommandHandle handle = resource->handle;
    if (!RegisterCommandRecord(std::move(resource), spec->flags))
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }

    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult Host::RegisterSource2Command(
    KeelPluginHandle plugin,
    const KeelSource2CommandSpec* spec,
    KeelCommandHandle* output)
{
    std::scoped_lock lock(state_mutex_);
    if (!accepting_resources_ || !adapter_)
    {
        return KEEL_RESULT_NOT_READY;
    }
    PluginRecord* owner = PluginByHandle(plugin);
    if (!spec || spec->size != sizeof(KeelSource2CommandSpec) || spec->reserved != 0 ||
        !output || !spec->name || !spec->callback || !owner)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (!owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (next_command_ == 0)
    {
        Write(KEEL_LOG_ERROR, "command handle space is exhausted");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    if (EqualInsensitive(spec->name, "keel"))
    {
        return KEEL_RESULT_RESERVED_NAME;
    }
    if (!ValidCommandName(spec->name) ||
        !ValidMetadataText(spec->description, 255, true))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (std::any_of(commands_.begin(), commands_.end(), [spec](const auto& entry) {
            return EqualInsensitive(entry.second->name, spec->name);
        }))
    {
        return KEEL_RESULT_ALREADY_EXISTS;
    }
    auto resource = std::make_unique<CommandRecord>();
    resource->handle = next_command_++;
    resource->owner = plugin;
    resource->name = spec->name;
    resource->description = spec->description ? spec->description : "";
    resource->native_callback = spec->callback;
    resource->user_data = spec->user_data;
    resource->enabled.store(!owner->loading && owner->state == PluginState::loaded);
    const KeelCommandHandle handle = resource->handle;
    if (!RegisterCommandRecord(std::move(resource), spec->flags))
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult Host::UnregisterCommand(
    KeelPluginHandle plugin,
    KeelCommandHandle command)
{
    std::scoped_lock lock(state_mutex_);
    if (command == 0 || !PluginByHandle(plugin))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    const auto iterator = commands_.find(command);
    if (iterator == commands_.end() || iterator->second->owner != plugin)
    {
        return KEEL_RESULT_NOT_FOUND;
    }

    std::unique_ptr<CommandRecord> resource = std::move(iterator->second);
    resource->enabled.store(false, std::memory_order_release);
    if (adapter_)
    {
        adapter_->UnregisterCommand(resource->game_handle);
    }
    resource->callback = nullptr;
    resource->native_callback = nullptr;
    resource->user_data = nullptr;
    commands_.erase(iterator);
    retired_commands_.push_back(std::move(resource));
    return KEEL_RESULT_OK;
}

bool Host::RegisterCommandRecord(std::unique_ptr<CommandRecord> resource, std::uint64_t flags)
{
    const GameCommandSpec game_spec{
        resource->name.c_str(),
        resource->description.c_str(),
        flags,
        &DispatchCommand,
        resource.get()
    };
    std::string error;
    if (!adapter_->RegisterCommand(game_spec, resource->game_handle, error))
    {
        Write(KEEL_LOG_ERROR, "command registration failed for " + resource->name + ": " + error);
        return false;
    }

    const KeelCommandHandle handle = resource->handle;
    commands_.emplace(handle, std::move(resource));
    return true;
}

bool Host::ValidCommandName(const char* name)
{
    std::size_t length{};
    for (const unsigned char* character = reinterpret_cast<const unsigned char*>(name);
         *character;
         ++character)
    {
        if (!(std::islower(*character) || std::isdigit(*character) || *character == '_'))
        {
            return false;
        }
        if (++length > 63)
        {
            return false;
        }
    }
    return length != 0;
}

void Host::WriteShutdownTrace(std::string_view event, std::string_view detail) noexcept
{
    platform::AppendShutdownTrace(event, detail);
}

void Host::Write(KeelLogLevel level, const std::string& message)
{
    std::string line = "[KeelS2] ";
    if (level == KEEL_LOG_WARNING)
    {
        line += "WARNING: ";
    }
    else if (level == KEEL_LOG_ERROR)
    {
        line += "ERROR: ";
    }
    line += message;
    WriteLine(line);
}

void Host::WriteUsage(const std::string& usage)
{
    WriteLine("[KeelS2] Usage: " + usage);
}

void Host::WriteLine(const std::string& message)
{
    std::scoped_lock lock(log_mutex_);
    const std::string line = message + "\n";
    platform::WriteEngineConsole(line.c_str());
}

bool BeginGameCommandDispatch() noexcept
{
    g_dispatch_entries.fetch_add(1, std::memory_order_acq_rel);
    try
    {
        if (Host::Instance().CommandDispatchOpen())
        {
            return true;
        }
    }
    catch (...)
    {
    }
    ReleaseDispatchEntry();
    return false;
}

void EndGameCommandDispatch() noexcept
{
    ReleaseDispatchEntry();
}

}

extern "C" KEELS2_HOST_EXPORT std::uint32_t KeelHost_Start(const KeelHostStartInfo* info)
{
    if (!info || info->size != sizeof(KeelHostStartInfo))
    {
        return 0;
    }
    try
    {
        return keels2::host::Host::Instance().Start(*info);
    }
    catch (...)
    {
        return 0;
    }
}

extern "C" KEELS2_HOST_EXPORT std::uint32_t KeelHost_CompleteStartup()
{
    try
    {
        return keels2::host::Host::Instance().CompleteStartup() ? 1u : 0u;
    }
    catch (...)
    {
        return 0;
    }
}

extern "C" KEELS2_HOST_EXPORT std::uint32_t KeelHost_Stop()
{
    try
    {
        return keels2::host::Host::Instance().Stop() ? 1u : 0u;
    }
    catch (...)
    {
        return 0;
    }
}
