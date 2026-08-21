#include "game_adapter.h"

#include <keels2/cs2/cvar_abi.h>
#include <keels2/cs2/native_bridge.h>
#include <keels2/keelhook.hpp>
#include <keels2/platform/console.h>
#include <keels2/platform/diagnostic_trace.h>
#include <keels2/platform/dynamic_library.h>
#include <keels2/platform/loaded_module.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace keels2::host
{

class Cs2Adapter final : public GameAdapter
{
private:
    using MemAllocStringDuplicate = char* (*)(const char*);
    using MemAllocFree = void (*)(void*);

    template <typename Function>
    static Function AddressFunction(void* address) noexcept
    {
        static_assert(sizeof(Function) == sizeof(address));
        Function function{};
        std::memcpy(&function, &address, sizeof(function));
        return function;
    }

    struct InterfaceEntry
    {
        KeelSource2Capability capability{};
        KeelSource2Factory factory{};
        void* instance{};
        std::string name;
        std::string module;
        std::string module_path;
    };

    class EngineCommandCallback final : public cs2::ICommandCallback
    {
    public:
        EngineCommandCallback(GameCommandCallback callback, void* user_data)
            : callback_(callback), user_data_(user_data)
        {
        }

        void CommandCallback(const void* context, const void* command) override
        {
            if (!BeginGameCommandDispatch())
            {
                return;
            }
            struct DispatchScope
            {
                ~DispatchScope()
                {
                    EndGameCommandDispatch();
                }
            } scope;

            GameCommandInvocation invocation{};
            invocation.context = context;
            invocation.command = command;
            std::int32_t argument_count{};
            const char* const* arguments{};
            if (command)
            {
                const auto* bytes = static_cast<const unsigned char*>(command);
                std::memcpy(
                    &argument_count,
                    bytes + cs2::kCommandArgumentCountOffset,
                    sizeof(argument_count)
                );
                std::memcpy(
                    &arguments,
                    bytes + cs2::kCommandArgumentValuesOffset,
                    sizeof(arguments)
                );
            }
            if (argument_count >= 0 &&
                argument_count <= static_cast<std::int32_t>(cs2::kCommandMaximumArguments) &&
                (argument_count == 0 || arguments))
            {
                invocation.argument_count = static_cast<std::uint32_t>(argument_count);
                invocation.arguments = arguments;
            }
            callback_(invocation, user_data_);
        }

    private:
        GameCommandCallback callback_;
        void* user_data_;
    };

    struct CommandEntry
    {
        CommandEntry(const GameCommandSpec& spec)
            : name(spec.name),
              description(spec.description ? spec.description : ""),
              callback(spec.callback, spec.user_data)
        {
        }

        std::string name;
        std::string description;
        EngineCommandCallback callback;
        cs2::CommandRef reference;
    };

    struct ConVarEntry
    {
        GameConVarHandle handle{};
        std::string name;
        std::string description;
        std::string default_string;
        std::string minimum_string;
        std::string maximum_string;
        KeelConVarType type{};
        std::uint64_t flags{};
        KeelConVarValue default_value{};
        KeelConVarValue minimum_value{};
        KeelConVarValue maximum_value{};
        bool has_minimum{};
        bool has_maximum{};
        bool owned{};
        GameConVarCallback callback{};
        GameNativeConVarCallback native_callback{};
        void* user_data{};
        cs2::ConVarObject object;
        std::atomic<bool> active{};
        std::atomic<bool> registering{true};
        std::atomic<std::uint32_t> provider_active{};
    };

    inline static std::mutex convar_callback_mutex_;
    inline static std::unordered_map<const cs2::ConVarData*, ConVarEntry*> convar_callbacks_;

    struct LifecycleHook
    {
        KeelHookTargetHandle target{};
        KeelHookCallbackHandle callback{};
    };

    struct Source2Hook
    {
        KeelHookTargetHandle target{};
        KeelHookCallbackHandle callback{};
    };

public:
    const char* Name() const override
    {
        return "cs2";
    }

    bool Start(
        KeelCreateInterfaceFn engine_factory,
        KeelCreateInterfaceFn server_factory,
        const KeelHostCompatibilityInfo& compatibility,
        std::string& error) override
    {
        Stop();
        if (!engine_factory)
        {
            error = "engine interface factory is null";
            return false;
        }
        if (!server_factory)
        {
            error = "server interface factory is null";
            return false;
        }
        if (!ValidCompatibility(compatibility))
        {
            error = "compatibility profile does not match the compiled CS2 interface and command ABI";
            return false;
        }

        InterfaceEntry server;
        InterfaceEntry game_clients;
        InterfaceEntry cvar;
        InterfaceEntry engine_service;
        if (!ResolveInterface(
                server_factory,
                KEELS2_SOURCE2_CAPABILITY_SERVER,
                KEELS2_SOURCE2_FACTORY_SERVER,
                compatibility.server_interface,
                compatibility.server_module,
                compatibility.server_validation_slot,
                server,
                error) ||
            !ResolveInterface(
                server_factory,
                KEELS2_SOURCE2_CAPABILITY_GAME_CLIENTS,
                KEELS2_SOURCE2_FACTORY_SERVER,
                compatibility.game_clients_interface,
                compatibility.server_module,
                compatibility.game_clients_validation_slot,
                game_clients,
                error) ||
            !ResolveInterface(
                engine_factory,
                KEELS2_SOURCE2_CAPABILITY_CVAR,
                KEELS2_SOURCE2_FACTORY_ENGINE,
                compatibility.cvar_interface,
                compatibility.cvar_module,
                compatibility.register_command_slot,
                cvar,
                error) ||
            !ResolveInterface(
                engine_factory,
                0,
                KEELS2_SOURCE2_FACTORY_ENGINE,
                compatibility.engine_service_interface,
                compatibility.engine_service_module,
                compatibility.engine_service_register_loop_mode_slot,
                engine_service,
                error))
        {
            return false;
        }

        platform::LoadedModule game_event_module;
        if (platform::FindLoadedModule(
                server.module_path,
                game_event_module,
                error) != platform::ModuleLookup::found)
        {
            error = "CGameEventManager module could not be resolved: " + error;
            return false;
        }
        platform::LoadedModulePin game_event_module_pin;
        if (!game_event_module_pin.Acquire(game_event_module, error))
        {
            error = "CGameEventManager module could not be pinned: " + error;
            return false;
        }
        void** game_event_manager_vtable{};
        const std::size_t game_event_entry_count =
            static_cast<std::size_t>((std::max)(
                compatibility.game_event_load_events_slot,
                compatibility.game_event_add_listener_slot)) + 1;
        if (platform::FindPrimaryVtable(
                game_event_module,
                compatibility.game_event_manager_class,
                game_event_entry_count,
                game_event_manager_vtable,
                error) != platform::ModuleLookup::found)
        {
            error = "CGameEventManager RTTI lookup failed: " + error;
            return false;
        }
        std::filesystem::path game_event_load_module;
        std::filesystem::path game_event_add_module;
        if (!ValidateTarget(
                game_event_manager_vtable[compatibility.game_event_load_events_slot],
                compatibility.game_event_module,
                "CGameEventManager::LoadEventsFromFile",
                game_event_load_module,
                error) ||
            !ValidateTarget(
                game_event_manager_vtable[compatibility.game_event_add_listener_slot],
                compatibility.game_event_module,
                "IGameEventManager2::AddListener",
                game_event_add_module,
                error) ||
            !SameModule(
                server.module_path,
                game_event_load_module,
                "CGameEventManager::LoadEventsFromFile",
                error) ||
            !SameModule(
                server.module_path,
                game_event_add_module,
                "IGameEventManager2::AddListener",
                error))
        {
            return false;
        }

        auto** cvar_vtable = *reinterpret_cast<void***>(cvar.instance);
        const auto validate_cvar_slot = [&](std::uint32_t slot, const char* operation) {
            std::filesystem::path module;
            return ValidateTarget(
                       cvar_vtable[slot],
                       compatibility.cvar_module,
                       operation,
                       module,
                       error) &&
                SameModule(cvar.module_path, module, "CVar interface", error);
        };
        if (!validate_cvar_slot(compatibility.find_convar_slot, "FindConVar") ||
            !validate_cvar_slot(compatibility.register_convar_slot, "RegisterConVar") ||
            !validate_cvar_slot(compatibility.unregister_convar_slot, "UnregisterConVarCallbacks") ||
            !validate_cvar_slot(compatibility.get_convar_data_slot, "GetConVarData") ||
            !validate_cvar_slot(compatibility.call_convar_change_slot, "CallChangeCallback") ||
            !validate_cvar_slot(compatibility.call_convar_filter_slot, "CallFilterCallback") ||
            !validate_cvar_slot(
                compatibility.call_global_convar_change_slot,
                "CallGlobalChangeCallbacks") ||
            !validate_cvar_slot(
                compatibility.queue_thread_set_value_slot,
                "QueueThreadSetValue") ||
            !validate_cvar_slot(
                compatibility.unregister_command_slot,
                "UnregisterConCommandCallbacks") ||
            !ValidateLifecycleSlot(
                server,
                compatibility.game_frame_slot,
                "Source2Server001::GameFrame",
                error) ||
            !ValidateLifecycleSlot(
                game_clients,
                compatibility.client_connected_slot,
                "Source2GameClients001::OnClientConnected",
                error) ||
            !ValidateLifecycleSlot(
                game_clients,
                compatibility.client_put_in_server_slot,
                "Source2GameClients001::ClientPutInServer",
                error) ||
            !ValidateLifecycleSlot(
                game_clients,
                compatibility.client_active_slot,
                "Source2GameClients001::ClientActive",
                error) ||
            !ValidateLifecycleSlot(
                game_clients,
                compatibility.client_fully_connected_slot,
                "Source2GameClients001::ClientFullyConnect",
                error) ||
            !ValidateLifecycleSlot(
                game_clients,
                compatibility.client_disconnecting_slot,
                "Source2GameClients001::ClientDisconnect",
                error) ||
            !ValidateLifecycleSlot(
                game_clients,
                compatibility.client_settings_changed_slot,
                "Source2GameClients001::ClientSettingsChanged",
                error) ||
            !ValidateLifecycleSlot(
                game_clients,
                compatibility.client_connect_slot,
                "Source2GameClients001::ClientConnect",
                error) ||
            !ValidateLifecycleSlot(
                game_clients,
                compatibility.client_command_slot,
                "Source2GameClients001::ClientCommand",
                error) ||
            !ValidateLifecycleSlot(
                engine_service,
                compatibility.engine_service_unregister_loop_mode_slot,
                "EngineServiceMgr001::UnregisterLoopMode",
                error))
        {
            return false;
        }

        void* string_duplicate_address = platform::ModuleSymbolFromAddress(
            cvar_vtable[compatibility.register_convar_slot],
            "MemAlloc_StrDupFunc",
            error);
        if (!string_duplicate_address)
        {
            error = "MemAlloc_StrDupFunc is unavailable from VEngineCvar007's module: " + error;
            return false;
        }
        void* memory_free_address = platform::ModuleSymbolFromAddress(
            cvar_vtable[compatibility.register_convar_slot],
            "MemAlloc_FreeFunc",
            error);
        if (!memory_free_address)
        {
            error = "MemAlloc_FreeFunc is unavailable from VEngineCvar007's module: " + error;
            return false;
        }
        std::filesystem::path string_duplicate_module;
        std::filesystem::path memory_free_module;
        if (!ValidateTarget(
                string_duplicate_address,
                compatibility.cvar_module,
                "MemAlloc_StrDupFunc",
                string_duplicate_module,
                error) ||
            !ValidateTarget(
                memory_free_address,
                compatibility.cvar_module,
                "MemAlloc_FreeFunc",
                memory_free_module,
                error) ||
            !SameModule(cvar.module_path, string_duplicate_module, "CVar string allocator", error) ||
            !SameModule(cvar.module_path, memory_free_module, "CVar string deallocator", error))
        {
            return false;
        }

        compatibility_profile_ = compatibility.profile;
        engine_factory_ = engine_factory;
        server_factory_ = server_factory;
        server_ = std::move(server);
        game_clients_ = std::move(game_clients);
        cvar_interface_ = std::move(cvar);
        engine_service_ = std::move(engine_service);
        game_event_module_pin_ = std::move(game_event_module_pin);
        game_event_manager_vtable_ = game_event_manager_vtable;
        cvar_ = static_cast<cs2::CvarInterface*>(cvar_interface_.instance);
        string_duplicate_ = AddressFunction<MemAllocStringDuplicate>(string_duplicate_address);
        memory_free_ = AddressFunction<MemAllocFree>(memory_free_address);
        game_frame_slot_ = compatibility.game_frame_slot;
        client_connected_slot_ = compatibility.client_connected_slot;
        client_put_in_server_slot_ = compatibility.client_put_in_server_slot;
        client_active_slot_ = compatibility.client_active_slot;
        client_fully_connected_slot_ = compatibility.client_fully_connected_slot;
        client_disconnecting_slot_ = compatibility.client_disconnecting_slot;
        client_settings_changed_slot_ = compatibility.client_settings_changed_slot;
        client_connect_slot_ = compatibility.client_connect_slot;
        client_command_slot_ = compatibility.client_command_slot;
        register_loop_mode_slot_ = compatibility.engine_service_register_loop_mode_slot;
        unregister_loop_mode_slot_ = compatibility.engine_service_unregister_loop_mode_slot;
        factory_create_slot_ = compatibility.loop_mode_factory_create_slot;
        factory_destroy_slot_ = compatibility.loop_mode_factory_destroy_slot;
        loop_init_slot_ = compatibility.loop_mode_init_slot;
        loop_shutdown_slot_ = compatibility.loop_mode_shutdown_slot;
        game_event_load_events_slot_ = compatibility.game_event_load_events_slot;
        game_event_add_listener_slot_ = compatibility.game_event_add_listener_slot;
        error.clear();
        return true;
    }

    bool CompleteStartup(std::string& error) override
    {
        std::scoped_lock lock(source2_mutex_);
        if (!game_event_error_.empty())
        {
            error = game_event_error_;
            return false;
        }
        if (!game_event_manager_ || !game_event_listener_)
        {
            error = "CGameEventManager::LoadEventsFromFile was not observed during Source2Server001::Init";
            return false;
        }
        if (bound_game_events_.size() != requested_game_events_.size())
        {
            error = "not every staged IGameEventManager2 listener was bound";
            return false;
        }
        error.clear();
        return true;
    }

    void Stop() noexcept override
    {
        ShutdownSource2Callbacks();
        const bool trace = cvar_ || server_.instance || game_clients_.instance ||
            cvar_interface_.instance || !compatibility_profile_.empty();
        if (trace)
        {
            platform::AppendShutdownTrace("cs2 adapter stop begin");
            platform::AppendShutdownTrace("cs2 active command release begin");
        }
        while (!commands_.empty())
        {
            UnregisterCommand(commands_.begin()->first);
        }
        if (trace)
        {
            platform::AppendShutdownTrace("cs2 active command release complete");
            platform::AppendShutdownTrace("cs2 active ConVar release begin");
        }
        while (!convars_.empty())
        {
            ReleaseConVar(convars_.begin()->first);
        }
        if (trace)
        {
            platform::AppendShutdownTrace("cs2 active ConVar release complete");
            platform::AppendShutdownTrace("cs2 retired command release begin");
        }
        retired_commands_.clear();
        if (trace)
        {
            platform::AppendShutdownTrace("cs2 retired command release complete");
            platform::AppendShutdownTrace("cs2 retired ConVar release begin");
        }
        retired_convars_.clear();
        if (trace)
        {
            platform::AppendShutdownTrace("cs2 retired ConVar release complete");
            platform::AppendShutdownTrace("cs2 interface invalidation begin");
        }
        string_duplicate_ = nullptr;
        memory_free_ = nullptr;
        cvar_ = nullptr;
        game_event_manager_vtable_ = nullptr;
        game_event_module_pin_.Release();
        named_interfaces_.clear();
        engine_factory_ = nullptr;
        server_factory_ = nullptr;
        engine_service_ = {};
        cvar_interface_ = {};
        game_clients_ = {};
        server_ = {};
        compatibility_profile_.clear();
        if (trace)
        {
            platform::AppendShutdownTrace("cs2 interface invalidation complete");
            platform::AppendShutdownTrace("cs2 lifecycle state reset begin");
        }
        lifecycle_hooks_ = {};
        lifecycle_callback_ = nullptr;
        lifecycle_user_data_ = nullptr;
        game_frame_observed_.store(false, std::memory_order_release);
        game_frame_slot_ = 0;
        client_connected_slot_ = 0;
        client_put_in_server_slot_ = 0;
        client_active_slot_ = 0;
        client_fully_connected_slot_ = 0;
        client_disconnecting_slot_ = 0;
        client_settings_changed_slot_ = 0;
        client_connect_slot_ = 0;
        client_command_slot_ = 0;
        register_loop_mode_slot_ = 0;
        unregister_loop_mode_slot_ = 0;
        factory_create_slot_ = 0;
        factory_destroy_slot_ = 0;
        loop_init_slot_ = 0;
        loop_shutdown_slot_ = 0;
        game_event_load_events_slot_ = 0;
        game_event_add_listener_slot_ = 0;
        source2_hooks_.clear();
        active_factories_.clear();
        active_loops_.clear();
        initialized_loops_.clear();
        hooked_factory_vtables_.clear();
        hooked_loop_vtables_.clear();
        source2_hooks_api_ = nullptr;
        source2_hook_owner_ = 0;
        if (trace)
        {
            platform::AppendShutdownTrace("cs2 lifecycle state reset complete");
            platform::AppendShutdownTrace("cs2 adapter stop complete");
        }
    }

    KeelResult QueryInterface(
        KeelSource2Capability capability,
        KeelSource2InterfaceInfo& info) const noexcept override
    {
        const InterfaceEntry* entry{};
        if (capability == KEELS2_SOURCE2_CAPABILITY_SERVER)
        {
            entry = &server_;
        }
        else if (capability == KEELS2_SOURCE2_CAPABILITY_GAME_CLIENTS)
        {
            entry = &game_clients_;
        }
        else if (capability == KEELS2_SOURCE2_CAPABILITY_CVAR)
        {
            entry = &cvar_interface_;
        }
        else
        {
            return KEEL_RESULT_UNSUPPORTED;
        }
        if (!entry->instance || compatibility_profile_.empty())
        {
            return KEEL_RESULT_NOT_READY;
        }
        return DescribeInterface(*entry, info);
    }

    KeelResult QueryNamedInterface(
        KeelSource2Factory factory,
        const char* interface_name,
        KeelSource2InterfaceInfo& info) override
    {
        if ((factory != KEELS2_SOURCE2_FACTORY_ENGINE &&
                factory != KEELS2_SOURCE2_FACTORY_SERVER) ||
            !ValidInterfaceName(interface_name))
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        KeelCreateInterfaceFn interface_factory = factory == KEELS2_SOURCE2_FACTORY_ENGINE
            ? engine_factory_
            : server_factory_;
        if (!interface_factory || compatibility_profile_.empty())
        {
            return KEEL_RESULT_NOT_READY;
        }
        std::pair<KeelSource2Factory, std::string> key{factory, interface_name};
        const auto cached = named_interfaces_.find(key);
        if (cached != named_interfaces_.end())
        {
            return DescribeInterface(cached->second, info);
        }

        int return_code = 1;
        void* instance = interface_factory(interface_name, &return_code);
        if (!instance)
        {
            return return_code == 0 ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_NOT_FOUND;
        }
        if (return_code != 0)
        {
            return KEEL_RESULT_ENGINE_FAILURE;
        }
        auto** vtable = *reinterpret_cast<void***>(instance);
        if (!vtable || !vtable[0])
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        std::filesystem::path module;
        std::string error;
        if (!platform::ModulePathFromAddress(vtable[0], module, error) ||
            module.filename().empty())
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        InterfaceEntry entry{
            KEELS2_SOURCE2_CAPABILITY_NAMED,
            factory,
            instance,
            interface_name,
            module.filename().string(),
            module.string()
        };
        const auto position = named_interfaces_.emplace(std::move(key), std::move(entry)).first;
        return DescribeInterface(position->second, info);
    }

    KeelResult EnableLifecycleEvent(
        KeelLifecycleEventType event,
        const KeelHookApi& hooks,
        KeelPluginHandle owner,
        GameLifecycleCallback callback,
        void* user_data,
        std::string& error) override
    {
        if (!ValidLifecycleEvent(event) || !callback || !server_.instance ||
            !game_clients_.instance || compatibility_profile_.empty() ||
            hooks.size != sizeof(KeelHookApi) || hooks.api_version != KEELHOOK_API_VERSION ||
            !hooks.resolve_virtual_target || !hooks.release_target || !hooks.add_callback)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (lifecycle_callback_ &&
            (lifecycle_callback_ != callback || lifecycle_user_data_ != user_data))
        {
            return KEEL_RESULT_BUSY;
        }
        LifecycleHook& state = lifecycle_hooks_[event];
        if (state.callback)
        {
            return KEEL_RESULT_OK;
        }

        void* instance{};
        std::uint32_t slot{};
        const KeelHookPrototype* prototype{};
        KeelHookCallback hook_callback{};
        std::uint32_t phase{KH_PHASE_POST};
        switch (event)
        {
            case KEELS2_LIFECYCLE_GAME_FRAME:
                instance = server_.instance;
                slot = game_frame_slot_;
                prototype = &kh::MethodPrototype<void(bool, bool, bool)>::value;
                hook_callback = &GameFrame;
                break;
            case KEELS2_LIFECYCLE_CLIENT_CONNECTED:
                instance = game_clients_.instance;
                slot = client_connected_slot_;
                prototype = &kh::MethodPrototype<
                    void(std::int32_t, const char*, std::uint64_t, const char*, const char*, bool)>::value;
                hook_callback = &ClientConnected;
                break;
            case KEELS2_LIFECYCLE_CLIENT_PUT_IN_SERVER:
                instance = game_clients_.instance;
                slot = client_put_in_server_slot_;
                prototype = &kh::MethodPrototype<
                    void(std::int32_t, const char*, std::int32_t, std::uint64_t)>::value;
                hook_callback = &ClientPutInServer;
                break;
            case KEELS2_LIFECYCLE_CLIENT_ACTIVE:
                instance = game_clients_.instance;
                slot = client_active_slot_;
                prototype = &kh::MethodPrototype<
                    void(std::int32_t, bool, const char*, std::uint64_t)>::value;
                hook_callback = &ClientActive;
                break;
            case KEELS2_LIFECYCLE_CLIENT_FULLY_CONNECTED:
                instance = game_clients_.instance;
                slot = client_fully_connected_slot_;
                prototype = &kh::MethodPrototype<void(std::int32_t)>::value;
                hook_callback = &ClientFullyConnected;
                break;
            case KEELS2_LIFECYCLE_CLIENT_DISCONNECTING:
                instance = game_clients_.instance;
                slot = client_disconnecting_slot_;
                prototype = &kh::MethodPrototype<
                    void(std::int32_t, std::int32_t, const char*, std::uint64_t, const char*)>::value;
                hook_callback = &ClientDisconnecting;
                phase = KH_PHASE_PRE;
                break;
            case KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED:
                instance = game_clients_.instance;
                slot = client_settings_changed_slot_;
                prototype = &kh::MethodPrototype<void(std::int32_t)>::value;
                hook_callback = &ClientSettingsChanged;
                break;
            default:
                return KEEL_RESULT_UNSUPPORTED;
        }

        const KeelHookVirtualTargetSpec target_spec{
            sizeof(KeelHookVirtualTargetSpec),
            KH_MECHANISM_VIRTUAL,
            0,
            slot,
            0,
            0,
            instance,
            compatibility_profile_.c_str()
        };
        KeelHookTargetHandle target{};
        KeelResult result = hooks.resolve_virtual_target(owner, &target_spec, prototype, &target);
        if (result != KEEL_RESULT_OK)
        {
            error = "lifecycle target resolution failed";
            return result;
        }

        lifecycle_callback_ = callback;
        lifecycle_user_data_ = user_data;
        const KeelHookCallbackSpec callback_spec{
            sizeof(KeelHookCallbackSpec),
            phase,
            0,
            0,
            hook_callback,
            this
        };
        KeelHookCallbackHandle callback_handle{};
        result = hooks.add_callback(owner, target, &callback_spec, &callback_handle);
        if (result != KEEL_RESULT_OK)
        {
            static_cast<void>(hooks.release_target(owner, target));
            if (!AnyLifecycleHook())
            {
                lifecycle_callback_ = nullptr;
                lifecycle_user_data_ = nullptr;
            }
            error = "lifecycle callback installation failed";
            return result;
        }
        state = {target, callback_handle};
        if (event == KEELS2_LIFECYCLE_GAME_FRAME)
        {
            platform::WriteEngineConsole(
                "[KeelS2] lifecycle GameFrame hook armed: Source2Server001 slot 19, shared vtable\n");
        }
        error.clear();
        return KEEL_RESULT_OK;
    }

    KeelResult InitializeSource2Callbacks(
        const KeelHookApi& hooks,
        KeelPluginHandle owner,
        GameSource2Callback callback,
        void* user_data,
        std::string& error) override
    {
        if (!callback || !engine_service_.instance || !game_event_manager_vtable_ ||
            !game_clients_.instance || compatibility_profile_.empty() ||
            hooks.size != sizeof(KeelHookApi) || hooks.api_version != KEELHOOK_API_VERSION ||
            !hooks.resolve_virtual_target || !hooks.release_target || !hooks.add_callback ||
            !hooks.remove_callback)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (source2_callback_)
        {
            return source2_callback_ == callback && source2_user_data_ == user_data
                ? KEEL_RESULT_OK
                : KEEL_RESULT_BUSY;
        }

        source2_callback_ = callback;
        source2_user_data_ = user_data;
        source2_hooks_api_ = &hooks;
        source2_hook_owner_ = owner;
        const auto install = [&](void* instance,
                                 std::uint32_t slot,
                                 const KeelHookPrototype& prototype,
                                 KeelHookCallback hook_callback,
                                 std::uint32_t phase,
                                 const char* operation) {
            Source2Hook hook;
            const KeelResult result = InstallSource2Hook(
                instance,
                slot,
                prototype,
                hook_callback,
                phase,
                hook,
                error);
            if (result == KEEL_RESULT_OK)
            {
                source2_hooks_.push_back(hook);
            }
            else if (error.empty())
            {
                error = std::string(operation) + " hook installation failed";
            }
            return result;
        };

        void* game_event_manager_view = game_event_manager_vtable_;
        KeelResult result = install(
            &game_event_manager_view,
            game_event_load_events_slot_,
            kh::MethodPrototype<int(const char*, bool)>::value,
            &LoadEventsFromFile,
            KH_PHASE_POST,
            "CGameEventManager::LoadEventsFromFile");
        if (result == KEEL_RESULT_OK)
        {
            result = install(
                engine_service_.instance,
                register_loop_mode_slot_,
                kh::MethodPrototype<void(const char*, void*, void**)>::value,
                &RegisterLoopMode,
                KH_PHASE_POST,
                "RegisterLoopMode");
        }
        if (result == KEEL_RESULT_OK)
        {
            result = install(
                engine_service_.instance,
                unregister_loop_mode_slot_,
                kh::MethodPrototype<void(const char*, void*, void**)>::value,
                &UnregisterLoopMode,
                KH_PHASE_PRE,
                "UnregisterLoopMode");
        }
        if (result == KEEL_RESULT_OK)
        {
            result = install(
                game_clients_.instance,
                client_connect_slot_,
                kh::MethodPrototype<
                    bool(std::int32_t, const char*, std::uint64_t, const char*, bool, void*)>::value,
                &ClientConnect,
                KH_PHASE_PRE,
                "ClientConnect");
        }
        if (result == KEEL_RESULT_OK)
        {
            result = install(
                game_clients_.instance,
                client_command_slot_,
                kh::MethodPrototype<void(std::int32_t, const void*)>::value,
                &ClientCommand,
                KH_PHASE_PRE,
                "ClientCommand");
        }
        if (result != KEEL_RESULT_OK)
        {
            RollbackSource2Hooks();
            source2_callback_ = nullptr;
            source2_user_data_ = nullptr;
            source2_hooks_api_ = nullptr;
            return result;
        }
        error.clear();
        return KEEL_RESULT_OK;
    }

    void ShutdownSource2Callbacks() noexcept override
    {
        {
            std::scoped_lock lock(source2_mutex_);
            source2_callback_ = nullptr;
            source2_user_data_ = nullptr;
            if (game_event_listener_)
            {
                KeelCs2_DestroyGameEventListener(game_event_listener_);
                game_event_listener_ = nullptr;
            }
            game_event_manager_ = nullptr;
            requested_game_events_.clear();
            requested_game_event_names_.clear();
            bound_game_events_.clear();
            game_event_error_.clear();
        }
    }

    KeelResult ListenForGameEvent(const char* name, std::string& error) override
    {
        if (!name || !name[0])
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        std::scoped_lock lock(source2_mutex_);
        const auto [iterator, inserted] = requested_game_event_names_.insert(name);
        if (!inserted)
        {
            error.clear();
            return KEEL_RESULT_OK;
        }
        requested_game_events_.push_back(*iterator);
        if (!game_event_listener_)
        {
            error.clear();
            return KEEL_RESULT_OK;
        }
        if (!BindGameEventLocked(*iterator, error))
        {
            requested_game_events_.pop_back();
            requested_game_event_names_.erase(iterator);
            return KEEL_RESULT_ENGINE_FAILURE;
        }
        error.clear();
        return KEEL_RESULT_OK;
    }

    bool RegisterCommand(const GameCommandSpec& spec, GameCommandHandle& command, std::string& error) override
    {
        if (!cvar_)
        {
            error = "CS2 command system is not ready";
            return false;
        }
        if (!spec.name || !spec.name[0] || !spec.callback)
        {
            error = "command definition is invalid";
            return false;
        }

        auto entry = std::make_unique<CommandEntry>(spec);
        cs2::CommandCreation creation{};
        creation.name = entry->name.c_str();
        creation.help = entry->description.c_str();
        creation.flags = spec.flags | cs2::kReleaseCommandFlag;
        creation.callback_info.callback.interface_pointer = &entry->callback;
        creation.callback_info.is_interface = true;

        entry->reference = cvar_->RegisterConCommand(creation, 0);
        if (!entry->reference.IsValid())
        {
            error = "VEngineCvar007 rejected the command";
            return false;
        }

        const GameCommandHandle handle = next_command_++;
        commands_.emplace(handle, std::move(entry));
        command = handle;
        error.clear();
        return true;
    }

    void UnregisterCommand(GameCommandHandle command) noexcept override
    {
        const auto iterator = commands_.find(command);
        if (iterator == commands_.end())
        {
            return;
        }
        if (cvar_)
        {
            cvar_->UnregisterConCommandCallbacks(iterator->second->reference);
        }
        retired_commands_.push_back(std::move(iterator->second));
        commands_.erase(iterator);
    }

    KeelResult CreateConVar(
        const KeelConVarSpec& spec,
        GameConVarCallback callback,
        GameNativeConVarCallback native_callback,
        void* user_data,
        GameConVarHandle& convar,
        void** native_convar,
        std::string& error) override
    {
        if (!cvar_ || !spec.name || !spec.name[0] || !ValidPublicType(spec.type) ||
            spec.default_value.size != sizeof(KeelConVarValue) ||
            spec.default_value.type != spec.type ||
            (spec.has_minimum != KEEL_FALSE && spec.has_minimum != KEEL_TRUE) ||
            (spec.has_maximum != KEEL_FALSE && spec.has_maximum != KEEL_TRUE) ||
            spec.reserved_minimum != 0 || spec.reserved_maximum != 0 ||
            (spec.has_minimum == KEEL_TRUE &&
                (spec.minimum_value.size != sizeof(KeelConVarValue) ||
                    spec.minimum_value.type != spec.type)) ||
            (spec.has_maximum == KEEL_TRUE &&
                (spec.maximum_value.size != sizeof(KeelConVarValue) ||
                    spec.maximum_value.type != spec.type)))
        {
            error = "ConVar definition is invalid";
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (next_convar_ == 0)
        {
            error = "CS2 ConVar handle space is exhausted";
            return KEEL_RESULT_ENGINE_FAILURE;
        }

        auto entry = std::make_unique<ConVarEntry>();
        entry->handle = next_convar_++;
        entry->name = spec.name;
        entry->description = spec.description ? spec.description : "";
        entry->type = spec.type;
        entry->flags = spec.flags | cs2::kReleaseCommandFlag;
        entry->has_minimum = spec.has_minimum == KEEL_TRUE;
        entry->has_maximum = spec.has_maximum == KEEL_TRUE;
        entry->owned = true;
        entry->callback = callback;
        entry->native_callback = native_callback;
        entry->user_data = user_data;
        RetainPublicValue(spec.default_value, entry->default_value, entry->default_string);
        if (entry->has_minimum)
        {
            RetainPublicValue(spec.minimum_value, entry->minimum_value, entry->minimum_string);
        }
        if (entry->has_maximum)
        {
            RetainPublicValue(spec.maximum_value, entry->maximum_value, entry->maximum_string);
        }

        cs2::ConVarCreation creation{};
        creation.name = entry->name.c_str();
        creation.help = entry->description.c_str();
        creation.flags = entry->flags;
        creation.value_info.version = 0;
        creation.value_info.has_default = true;
        creation.value_info.has_minimum = entry->has_minimum;
        creation.value_info.has_maximum = entry->has_maximum;
        StoreEngineValue(entry->default_value, creation.value_info.default_value);
        if (entry->has_minimum)
        {
            StoreEngineValue(entry->minimum_value, creation.value_info.minimum_value);
        }
        if (entry->has_maximum)
        {
            StoreEngineValue(entry->maximum_value, creation.value_info.maximum_value);
        }

        std::unique_ptr<char, MemAllocFree> engine_default_string(nullptr, memory_free_);
        std::unique_ptr<char, MemAllocFree> engine_minimum_string(nullptr, memory_free_);
        std::unique_ptr<char, MemAllocFree> engine_maximum_string(nullptr, memory_free_);
        if (entry->type == KEELS2_CONVAR_STRING)
        {
            if (!string_duplicate_ || !memory_free_)
            {
                error = "VEngineCvar007 string allocator is unavailable";
                return KEEL_RESULT_ENGINE_FAILURE;
            }

            engine_default_string.reset(string_duplicate_(
                entry->default_value.value.string_value));
            if (!engine_default_string)
            {
                error = "VEngineCvar007 could not allocate the ConVar default string";
                return KEEL_RESULT_ENGINE_FAILURE;
            }
            cs2::ConVarValue default_value{};
            default_value.string = engine_default_string.get();
            std::memcpy(
                creation.value_info.default_value,
                &default_value,
                sizeof(default_value));

            if (entry->has_minimum)
            {
                engine_minimum_string.reset(string_duplicate_(
                    entry->minimum_value.value.string_value));
                if (!engine_minimum_string)
                {
                    error = "VEngineCvar007 could not allocate the ConVar minimum string";
                    return KEEL_RESULT_ENGINE_FAILURE;
                }
                cs2::ConVarValue minimum_value{};
                minimum_value.string = engine_minimum_string.get();
                std::memcpy(
                    creation.value_info.minimum_value,
                    &minimum_value,
                    sizeof(minimum_value));
            }
            if (entry->has_maximum)
            {
                engine_maximum_string.reset(string_duplicate_(
                    entry->maximum_value.value.string_value));
                if (!engine_maximum_string)
                {
                    error = "VEngineCvar007 could not allocate the ConVar maximum string";
                    return KEEL_RESULT_ENGINE_FAILURE;
                }
                cs2::ConVarValue maximum_value{};
                maximum_value.string = engine_maximum_string.get();
                std::memcpy(
                    creation.value_info.maximum_value,
                    &maximum_value,
                    sizeof(maximum_value));
            }
        }
        if (callback || native_callback)
        {
            creation.value_info.change_provider = &ConVarChangeProvider;
            creation.value_info.change_callback = &ConVarChange;
        }
        creation.value_info.type = EngineType(entry->type);

        cvar_->RegisterConVar(
            creation,
            0,
            &entry->object.reference,
            &entry->object.data);
        static_cast<void>(engine_default_string.release());
        static_cast<void>(engine_minimum_string.release());
        static_cast<void>(engine_maximum_string.release());
        entry->registering.store(false, std::memory_order_release);
        if (!entry->object.reference.IsValid() || !entry->object.data ||
            cvar_->GetConVarData(entry->object.reference) != entry->object.data)
        {
            if (entry->object.reference.IsValid())
            {
                cvar_->UnregisterConVarCallbacks(entry->object.reference);
                retired_convars_.push_back(std::move(entry));
            }
            error = "VEngineCvar007 rejected the ConVar";
            return KEEL_RESULT_ENGINE_FAILURE;
        }
        if (PublicType(entry->object.data->type) != entry->type)
        {
            cvar_->UnregisterConVarCallbacks(entry->object.reference);
            retired_convars_.push_back(std::move(entry));
            error = "the existing Source 2 ConVar has an incompatible type";
            return KEEL_RESULT_INCOMPATIBLE;
        }

        bool callback_registered = !entry->callback && !entry->native_callback;
        if (entry->callback || entry->native_callback)
        {
            const std::lock_guard lock(convar_callback_mutex_);
            callback_registered = convar_callbacks_.emplace(
                entry->object.data,
                entry.get()).second;
        }
        if (!callback_registered)
        {
            cvar_->UnregisterConVarCallbacks(entry->object.reference);
            retired_convars_.push_back(std::move(entry));
            error = "the Source 2 ConVar already has an active KeelS2 owner";
            return KEEL_RESULT_ALREADY_EXISTS;
        }

        entry->active.store(true, std::memory_order_release);
        const GameConVarHandle handle = entry->handle;
        void* native = &entry->object;
        convars_.emplace(handle, std::move(entry));
        convar = handle;
        if (native_convar)
        {
            *native_convar = native;
        }
        error.clear();
        return KEEL_RESULT_OK;
    }

    KeelResult FindConVar(
        const char* name,
        KeelConVarType expected_type,
        GameConVarHandle& convar,
        void** native_convar,
        std::string& error) override
    {
        if (!cvar_ || !name || !name[0] || !ValidPublicType(expected_type))
        {
            error = "ConVar reference request is invalid";
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (next_convar_ == 0)
        {
            error = "CS2 ConVar handle space is exhausted";
            return KEEL_RESULT_ENGINE_FAILURE;
        }
        const cs2::ConVarRef reference = cvar_->FindConVar(name, false);
        cs2::ConVarData* data = reference.IsValid() ? cvar_->GetConVarData(reference) : nullptr;
        if (!reference.IsValid() || !data)
        {
            error = "Source 2 ConVar was not found";
            return KEEL_RESULT_NOT_FOUND;
        }
        if (PublicType(data->type) != expected_type)
        {
            error = "Source 2 ConVar type is incompatible";
            return KEEL_RESULT_INCOMPATIBLE;
        }

        auto entry = std::make_unique<ConVarEntry>();
        entry->handle = next_convar_++;
        entry->name = name;
        entry->type = expected_type;
        entry->owned = false;
        entry->object.reference = reference;
        entry->object.data = data;
        entry->registering.store(false, std::memory_order_release);
        const GameConVarHandle handle = entry->handle;
        void* native = &entry->object;
        convars_.emplace(handle, std::move(entry));
        convar = handle;
        if (native_convar)
        {
            *native_convar = native;
        }
        error.clear();
        return KEEL_RESULT_OK;
    }

    void ReleaseConVar(GameConVarHandle convar) noexcept override
    {
        const auto iterator = convars_.find(convar);
        if (iterator == convars_.end())
        {
            return;
        }
        std::unique_ptr<ConVarEntry> entry = std::move(iterator->second);
        entry->active.store(false, std::memory_order_release);
        if (entry->owned && cvar_ && entry->object.reference.IsValid())
        {
            cvar_->UnregisterConVarCallbacks(entry->object.reference);
        }
        if (entry->owned && (entry->callback || entry->native_callback) && entry->object.data)
        {
            const std::lock_guard lock(convar_callback_mutex_);
            const auto callback = convar_callbacks_.find(entry->object.data);
            if (callback != convar_callbacks_.end() && callback->second == entry.get())
            {
                convar_callbacks_.erase(callback);
            }
        }
        WaitForConVarProviders(entry->provider_active);
        convars_.erase(iterator);
        retired_convars_.push_back(std::move(entry));
    }

    KeelResult ReadConVar(
        GameConVarHandle convar,
        std::int32_t slot,
        KeelConVarValue& value) const noexcept override
    {
        const ConVarEntry* entry = ConVarByHandle(convar);
        if (!entry || !entry->object.data)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        if (slot != KEELS2_CONVAR_GLOBAL_SLOT && slot != 0)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        cs2::ConVarValue engine_value{};
        if (!LoadLiveEngineValue(entry->type, entry->object.data->values, engine_value))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        return LoadPublicValue(entry->type, &engine_value, value);
    }

    KeelResult QueueConVarSet(
        GameConVarHandle convar,
        std::int32_t slot,
        const KeelConVarValue& value) noexcept override
    {
        try
        {
            ConVarEntry* entry = ConVarByHandle(convar);
            if (!entry || !entry->object.data)
            {
                return KEEL_RESULT_NOT_FOUND;
            }
            if ((slot != KEELS2_CONVAR_GLOBAL_SLOT && slot != 0) ||
                value.size != sizeof(KeelConVarValue) || value.type != entry->type ||
                (value.type == KEELS2_CONVAR_STRING && !value.value.string_value))
            {
                return KEEL_RESULT_INVALID_ARGUMENT;
            }

            cs2::ConVarValue engine_value{};
            PublicToEngine(value, engine_value);
            constexpr std::int32_t engine_global_slot = 0;
            if ((entry->object.data->flags & cs2::kPerformingCallbacksFlag) != 0)
            {
                ClampEngineValue(entry->type, *entry->object.data, engine_value);
                cs2::ConVarValue current{};
                if (!LoadLiveEngineValue(
                        entry->type,
                        entry->object.data->values,
                        current))
                {
                    return KEEL_RESULT_INCOMPATIBLE;
                }
                if (EqualEngineValue(entry->type, current, engine_value))
                {
                    return KEEL_RESULT_OK;
                }
                cvar_->QueueThreadSetValue(
                    &entry->object,
                    engine_global_slot,
                    nullptr,
                    &engine_value);
                return KEEL_RESULT_OK;
            }
            return SetConVarNow(
                *entry,
                engine_value,
                slot,
                engine_global_slot);
        }
        catch (...)
        {
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }

    KeelResult DescribeConVar(
        GameConVarHandle convar,
        KeelConVarInfo& info) const noexcept override
    {
        const ConVarEntry* entry = ConVarByHandle(convar);
        const cs2::ConVarData* data = entry ? entry->object.data : nullptr;
        if (!entry || !data)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        KeelConVarValue default_value{};
        KeelConVarValue minimum_value{};
        KeelConVarValue maximum_value{};
        if (LoadPublicValue(entry->type, data->default_value, default_value) != KEEL_RESULT_OK ||
            (data->minimum_value &&
                LoadPublicValue(entry->type, data->minimum_value, minimum_value) != KEEL_RESULT_OK) ||
            (data->maximum_value &&
                LoadPublicValue(entry->type, data->maximum_value, maximum_value) != KEEL_RESULT_OK))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        info = {
            sizeof(KeelConVarInfo),
            entry->type,
            data->name,
            data->help,
            data->flags,
            default_value,
            data->minimum_value ? KEEL_TRUE : KEEL_FALSE,
            0,
            minimum_value,
            data->maximum_value ? KEEL_TRUE : KEEL_FALSE,
            0,
            maximum_value
        };
        return KEEL_RESULT_OK;
    }

private:
    static bool ValidPublicType(KeelConVarType type) noexcept
    {
        return type == KEELS2_CONVAR_BOOL || type == KEELS2_CONVAR_INT32 ||
            type == KEELS2_CONVAR_FLOAT32 || type == KEELS2_CONVAR_STRING;
    }

    static cs2::ConVarType EngineType(KeelConVarType type) noexcept
    {
        switch (type)
        {
            case KEELS2_CONVAR_BOOL:
                return cs2::ConVarType::boolean;
            case KEELS2_CONVAR_INT32:
                return cs2::ConVarType::int32;
            case KEELS2_CONVAR_FLOAT32:
                return cs2::ConVarType::float32;
            case KEELS2_CONVAR_STRING:
                return cs2::ConVarType::string;
            default:
                return cs2::ConVarType::invalid;
        }
    }

    static KeelConVarType PublicType(cs2::ConVarType type) noexcept
    {
        switch (type)
        {
            case cs2::ConVarType::boolean:
                return KEELS2_CONVAR_BOOL;
            case cs2::ConVarType::int32:
                return KEELS2_CONVAR_INT32;
            case cs2::ConVarType::float32:
                return KEELS2_CONVAR_FLOAT32;
            case cs2::ConVarType::string:
                return KEELS2_CONVAR_STRING;
            default:
                return 0;
        }
    }

    static void RetainPublicValue(
        const KeelConVarValue& source,
        KeelConVarValue& destination,
        std::string& string_storage)
    {
        destination = source;
        if (source.type == KEELS2_CONVAR_STRING)
        {
            string_storage = source.value.string_value ? source.value.string_value : "";
            destination.value.string_value = string_storage.c_str();
        }
    }

    static void PublicToEngine(
        const KeelConVarValue& source,
        cs2::ConVarValue& destination) noexcept
    {
        switch (source.type)
        {
            case KEELS2_CONVAR_BOOL:
                destination.boolean = source.value.boolean_value != KEEL_FALSE;
                break;
            case KEELS2_CONVAR_INT32:
                destination.int32 = source.value.int32_value;
                break;
            case KEELS2_CONVAR_FLOAT32:
                destination.float32 = source.value.float32_value;
                break;
            case KEELS2_CONVAR_STRING:
                destination.string = const_cast<char*>(source.value.string_value);
                break;
            default:
                break;
        }
    }

    static void StoreEngineValue(
        const KeelConVarValue& source,
        std::byte (&destination)[sizeof(cs2::ConVarValue)]) noexcept
    {
        cs2::ConVarValue value{};
        PublicToEngine(source, value);
        std::memcpy(destination, &value, sizeof(value));
    }

    static bool LoadLiveEngineValue(
        KeelConVarType type,
        const void* source,
        cs2::ConVarValue& destination) noexcept
    {
        if (!source)
        {
            return false;
        }
        destination = {};
        switch (type)
        {
            case KEELS2_CONVAR_BOOL:
                std::memcpy(&destination.boolean, source, sizeof(destination.boolean));
                return true;
            case KEELS2_CONVAR_INT32:
                std::memcpy(&destination.int32, source, sizeof(destination.int32));
                return true;
            case KEELS2_CONVAR_FLOAT32:
                std::memcpy(&destination.float32, source, sizeof(destination.float32));
                return true;
            case KEELS2_CONVAR_STRING:
                std::memcpy(&destination.string, source, sizeof(destination.string));
                return true;
            default:
                return false;
        }
    }

    static bool StoreLiveEngineValue(
        KeelConVarType type,
        void* destination,
        const cs2::ConVarValue& source) noexcept
    {
        if (!destination)
        {
            return false;
        }
        switch (type)
        {
            case KEELS2_CONVAR_BOOL:
                std::memcpy(destination, &source.boolean, sizeof(source.boolean));
                return true;
            case KEELS2_CONVAR_INT32:
                std::memcpy(destination, &source.int32, sizeof(source.int32));
                return true;
            case KEELS2_CONVAR_FLOAT32:
                std::memcpy(destination, &source.float32, sizeof(source.float32));
                return true;
            case KEELS2_CONVAR_STRING:
                std::memcpy(destination, &source.string, sizeof(source.string));
                return true;
            default:
                return false;
        }
    }

    static bool EqualEngineValue(
        KeelConVarType type,
        const cs2::ConVarValue& first,
        const cs2::ConVarValue& second) noexcept
    {
        switch (type)
        {
            case KEELS2_CONVAR_BOOL:
                return first.boolean == second.boolean;
            case KEELS2_CONVAR_INT32:
                return first.int32 == second.int32;
            case KEELS2_CONVAR_FLOAT32:
                return first.float32 == second.float32;
            case KEELS2_CONVAR_STRING:
                return std::strcmp(
                    first.string ? first.string : "",
                    second.string ? second.string : "") == 0;
            default:
                return false;
        }
    }

    static void ClampEngineValue(
        KeelConVarType type,
        const cs2::ConVarData& data,
        cs2::ConVarValue& value) noexcept
    {
        if (type == KEELS2_CONVAR_INT32)
        {
            if (data.minimum_value)
            {
                value.int32 = std::max(value.int32, data.minimum_value->int32);
            }
            if (data.maximum_value)
            {
                value.int32 = std::min(value.int32, data.maximum_value->int32);
            }
        }
        else if (type == KEELS2_CONVAR_FLOAT32)
        {
            if (data.minimum_value)
            {
                value.float32 = std::max(value.float32, data.minimum_value->float32);
            }
            if (data.maximum_value)
            {
                value.float32 = std::min(value.float32, data.maximum_value->float32);
            }
        }
    }

    static std::string EngineValueText(
        KeelConVarType type,
        const cs2::ConVarValue& value)
    {
        std::array<char, 64> buffer{};
        switch (type)
        {
            case KEELS2_CONVAR_BOOL:
                return value.boolean ? "true" : "false";
            case KEELS2_CONVAR_INT32:
                std::snprintf(buffer.data(), buffer.size(), "%d", value.int32);
                return buffer.data();
            case KEELS2_CONVAR_FLOAT32:
                std::snprintf(
                    buffer.data(),
                    buffer.size(),
                    "%f",
                    static_cast<double>(value.float32));
                return buffer.data();
            case KEELS2_CONVAR_STRING:
                return value.string ? value.string : "";
            default:
                return {};
        }
    }

    KeelResult SetConVarNow(
        ConVarEntry& entry,
        const cs2::ConVarValue& requested,
        std::int32_t filter_slot,
        std::int32_t callback_slot)
    {
        if (!cvar_ || !string_duplicate_ || !memory_free_ || !entry.object.data)
        {
            return KEEL_RESULT_NOT_READY;
        }

        auto* current = reinterpret_cast<cs2::ConVarValue*>(entry.object.data->values);
        cs2::ConVarValue previous{};
        cs2::ConVarValue candidate{};
        std::unique_ptr<char, MemAllocFree> previous_string(nullptr, memory_free_);
        std::unique_ptr<char, MemAllocFree> candidate_string(nullptr, memory_free_);
        if (entry.type == KEELS2_CONVAR_STRING)
        {
            previous_string.reset(string_duplicate_(current->string ? current->string : ""));
            candidate_string.reset(string_duplicate_(requested.string ? requested.string : ""));
            previous.string = previous_string.get();
            candidate.string = candidate_string.get();
            if (!previous.string || !candidate.string)
            {
                return KEEL_RESULT_ENGINE_FAILURE;
            }
        }
        else
        {
            if (!LoadLiveEngineValue(entry.type, current, previous))
            {
                return KEEL_RESULT_INCOMPATIBLE;
            }
            candidate = requested;
        }

        if (!cvar_->CallFilterCallback(
                entry.object.reference,
                filter_slot,
                &candidate,
                &previous,
                nullptr))
        {
            return KEEL_RESULT_OK;
        }

        cs2::ConVarValue effective = candidate;
        ClampEngineValue(entry.type, *entry.object.data, effective);
        const bool changed = !EqualEngineValue(entry.type, effective, previous);
        std::string old_text;
        std::string new_text;
        if (changed)
        {
            old_text = EngineValueText(entry.type, previous);
            new_text = EngineValueText(entry.type, effective);
        }

        std::unique_ptr<char, MemAllocFree> replacement(nullptr, memory_free_);
        if (entry.type == KEELS2_CONVAR_STRING)
        {
            replacement.reset(string_duplicate_(effective.string ? effective.string : ""));
            if (!replacement)
            {
                return KEEL_RESULT_ENGINE_FAILURE;
            }
            memory_free_(current->string);
            current->string = replacement.release();
        }
        else if (!StoreLiveEngineValue(entry.type, current, effective))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        if (!changed)
        {
            return KEEL_RESULT_OK;
        }

        ++entry.object.data->times_changed;
        cvar_->CallChangeCallback(
            entry.object.reference,
            callback_slot,
            current,
            &previous,
            nullptr);
        cvar_->CallGlobalChangeCallbacks(
            &entry.object,
            callback_slot,
            new_text.c_str(),
            old_text.c_str(),
            nullptr);
        return KEEL_RESULT_OK;
    }

    static KeelResult LoadPublicValue(
        KeelConVarType type,
        const cs2::ConVarValue* source,
        KeelConVarValue& destination) noexcept
    {
        if (!source || !ValidPublicType(type))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        destination = {};
        destination.size = sizeof(KeelConVarValue);
        destination.type = type;
        switch (type)
        {
            case KEELS2_CONVAR_BOOL:
                destination.value.boolean_value = source->boolean ? KEEL_TRUE : KEEL_FALSE;
                break;
            case KEELS2_CONVAR_INT32:
                destination.value.int32_value = source->int32;
                break;
            case KEELS2_CONVAR_FLOAT32:
                destination.value.float32_value = source->float32;
                break;
            case KEELS2_CONVAR_STRING:
                destination.value.string_value = source->string ? source->string : "";
                break;
            default:
                return KEEL_RESULT_INCOMPATIBLE;
        }
        return KEEL_RESULT_OK;
    }

    static void ConVarChange(
        cs2::ConVarObject*,
        std::int32_t,
        const cs2::ConVarValue*,
        const cs2::ConVarValue*)
    {
    }

    static void ConVarChangeProvider(
        cs2::ConVarObject* reference,
        std::int32_t split_screen_slot,
        const cs2::ConVarValue* new_value,
        const cs2::ConVarValue* old_value,
        void*,
        cs2::GenericChangeCallback)
    {
        try
        {
            ConVarEntry* entry{};
            if (reference && reference->data)
            {
                const std::lock_guard lock(convar_callback_mutex_);
                const auto callback = convar_callbacks_.find(reference->data);
                if (callback != convar_callbacks_.end())
                {
                    entry = callback->second;
                    entry->provider_active.fetch_add(1, std::memory_order_acq_rel);
                }
            }
            if (!entry)
            {
                return;
            }
            struct ProviderScope
            {
                ~ProviderScope()
                {
                    if (active->fetch_sub(1, std::memory_order_acq_rel) == 1)
                    {
                        active->notify_all();
                    }
                }

                std::atomic<std::uint32_t>* active;
            } provider_scope{&entry->provider_active};
            if (entry->registering.load(std::memory_order_acquire) ||
                !entry->active.load(std::memory_order_acquire) ||
                (!entry->callback && !entry->native_callback))
            {
                return;
            }
            if (split_screen_slot != KEELS2_CONVAR_GLOBAL_SLOT && split_screen_slot != 0)
            {
                return;
            }
            if (entry->native_callback)
            {
                entry->native_callback(
                    &entry->object,
                    split_screen_slot,
                    new_value,
                    old_value,
                    entry->user_data);
            }
            if (entry->callback)
            {
                KeelConVarValue public_new{};
                KeelConVarValue public_old{};
                if (LoadPublicValue(entry->type, new_value, public_new) != KEEL_RESULT_OK ||
                    LoadPublicValue(entry->type, old_value, public_old) != KEEL_RESULT_OK)
                {
                    return;
                }
                entry->callback(
                    KEELS2_CONVAR_GLOBAL_SLOT,
                    public_new,
                    public_old,
                    entry->user_data);
            }
        }
        catch (...)
        {
        }
    }

    static void WaitForConVarProviders(std::atomic<std::uint32_t>& active) noexcept
    {
        std::uint32_t value = active.load(std::memory_order_acquire);
        while (value != 0)
        {
            active.wait(value, std::memory_order_acquire);
            value = active.load(std::memory_order_acquire);
        }
    }

    ConVarEntry* ConVarByHandle(GameConVarHandle convar) noexcept
    {
        const auto iterator = convars_.find(convar);
        return iterator == convars_.end() ? nullptr : iterator->second.get();
    }

    const ConVarEntry* ConVarByHandle(GameConVarHandle convar) const noexcept
    {
        const auto iterator = convars_.find(convar);
        return iterator == convars_.end() ? nullptr : iterator->second.get();
    }

    KeelResult InstallSource2Hook(
        void* instance,
        std::uint32_t slot,
        const KeelHookPrototype& prototype,
        KeelHookCallback callback,
        std::uint32_t phase,
        Source2Hook& output,
        std::string& error)
    {
        if (!source2_hooks_api_ || !instance || !callback)
        {
            return KEEL_RESULT_NOT_READY;
        }
        const KeelHookVirtualTargetSpec target_spec{
            sizeof(KeelHookVirtualTargetSpec),
            KH_MECHANISM_VIRTUAL,
            0,
            slot,
            0,
            0,
            instance,
            compatibility_profile_.c_str()
        };
        KeelHookTargetHandle target{};
        KeelResult result = source2_hooks_api_->resolve_virtual_target(
            source2_hook_owner_,
            &target_spec,
            &prototype,
            &target);
        if (result != KEEL_RESULT_OK)
        {
            error = "Source 2 callback target resolution failed";
            return result;
        }
        const KeelHookCallbackSpec callback_spec{
            sizeof(KeelHookCallbackSpec),
            phase,
            0,
            0,
            callback,
            this
        };
        KeelHookCallbackHandle callback_handle{};
        result = source2_hooks_api_->add_callback(
            source2_hook_owner_,
            target,
            &callback_spec,
            &callback_handle);
        if (result != KEEL_RESULT_OK)
        {
            static_cast<void>(source2_hooks_api_->release_target(source2_hook_owner_, target));
            error = "Source 2 callback installation failed";
            return result;
        }
        output = {target, callback_handle};
        return KEEL_RESULT_OK;
    }

    void RollbackSource2Hooks() noexcept
    {
        if (source2_hooks_api_)
        {
            for (auto iterator = source2_hooks_.rbegin(); iterator != source2_hooks_.rend(); ++iterator)
            {
                static_cast<void>(source2_hooks_api_->remove_callback(
                    source2_hook_owner_,
                    iterator->callback));
                static_cast<void>(source2_hooks_api_->release_target(
                    source2_hook_owner_,
                    iterator->target));
            }
        }
        source2_hooks_.clear();
    }

    void EnsureFactoryHooks(void* factory)
    {
        if (!factory)
        {
            return;
        }
        void* table = *reinterpret_cast<void**>(factory);
        {
            std::scoped_lock lock(source2_mutex_);
            active_factories_.insert(factory);
            if (!table || !hooked_factory_vtables_.insert(table).second)
            {
                return;
            }
        }

        std::string error;
        Source2Hook create;
        KeelResult result = InstallSource2Hook(
            factory,
            factory_create_slot_,
            kh::MethodPrototype<void*()>::value,
            &CreateLoopMode,
            KH_PHASE_POST,
            create,
            error);
        if (result == KEEL_RESULT_OK)
        {
            Source2Hook destroy;
            result = InstallSource2Hook(
                factory,
                factory_destroy_slot_,
                kh::MethodPrototype<void(void*)>::value,
                &DestroyLoopMode,
                KH_PHASE_PRE,
                destroy,
                error);
            if (result == KEEL_RESULT_OK)
            {
                std::scoped_lock lock(source2_mutex_);
                source2_hooks_.push_back(create);
                source2_hooks_.push_back(destroy);
                return;
            }
            static_cast<void>(source2_hooks_api_->remove_callback(source2_hook_owner_, create.callback));
            static_cast<void>(source2_hooks_api_->release_target(source2_hook_owner_, create.target));
        }
        {
            std::scoped_lock lock(source2_mutex_);
            hooked_factory_vtables_.erase(table);
        }
        const std::string message =
            "[KeelS2] EngineServiceMgr game loop factory hook failed: " + error + "\n";
        platform::WriteEngineConsole(message.c_str());
    }

    void EnsureLoopHooks(void* loop)
    {
        if (!loop)
        {
            return;
        }
        void* table = *reinterpret_cast<void**>(loop);
        {
            std::scoped_lock lock(source2_mutex_);
            active_loops_.insert(loop);
            if (!table || !hooked_loop_vtables_.insert(table).second)
            {
                return;
            }
        }

        std::string error;
        Source2Hook init;
        KeelResult result = InstallSource2Hook(
            loop,
            loop_init_slot_,
            kh::MethodPrototype<bool(void*, void*)>::value,
            &LoopInit,
            KH_PHASE_POST,
            init,
            error);
        if (result == KEEL_RESULT_OK)
        {
            Source2Hook shutdown;
            result = InstallSource2Hook(
                loop,
                loop_shutdown_slot_,
                kh::MethodPrototype<void()>::value,
                &LoopShutdown,
                KH_PHASE_PRE,
                shutdown,
                error);
            if (result == KEEL_RESULT_OK)
            {
                std::scoped_lock lock(source2_mutex_);
                source2_hooks_.push_back(init);
                source2_hooks_.push_back(shutdown);
                return;
            }
            static_cast<void>(source2_hooks_api_->remove_callback(source2_hook_owner_, init.callback));
            static_cast<void>(source2_hooks_api_->release_target(source2_hook_owner_, init.target));
        }
        {
            std::scoped_lock lock(source2_mutex_);
            hooked_loop_vtables_.erase(table);
        }
        const std::string message =
            "[KeelS2] EngineServiceMgr game loop hook failed: " + error + "\n";
        platform::WriteEngineConsole(message.c_str());
    }

    template <typename Payload>
    KeelBool EmitSource2(KeelSource2CallbackType type, Payload& payload)
    {
        if (!source2_callback_)
        {
            return KEEL_TRUE;
        }
        KeelSource2CallbackEvent event{
            sizeof(KeelSource2CallbackEvent),
            type,
            sizeof(Payload),
            0,
            &payload
        };
        return source2_callback_(event, source2_user_data_);
    }

    bool BindGameEventLocked(const std::string& name, std::string& error)
    {
        if (bound_game_events_.contains(name))
        {
            error.clear();
            return true;
        }
        if (!game_event_listener_ || !KeelCs2_ListenForGameEvent(game_event_listener_, name.c_str()))
        {
            error = "IGameEventManager2 rejected listener for " + name;
            return false;
        }
        bound_game_events_.insert(name);
        error.clear();
        return true;
    }

    void CaptureGameEventManager(void* manager)
    {
        std::scoped_lock lock(source2_mutex_);
        if (!manager || !game_event_error_.empty())
        {
            return;
        }
        auto** vtable = *reinterpret_cast<void***>(manager);
        if (vtable != game_event_manager_vtable_)
        {
            game_event_error_ = "CGameEventManager instance does not use the pinned primary virtual table";
            return;
        }
        if (game_event_manager_)
        {
            if (game_event_manager_ != manager)
            {
                game_event_error_ = "CGameEventManager changed during server initialization";
            }
            return;
        }
        game_event_manager_ = manager;
        game_event_listener_ = KeelCs2_CreateGameEventListener(
            manager,
            &GameEventDispatch,
            this);
        if (!game_event_listener_)
        {
            game_event_error_ = "IGameEventListener2 creation failed";
            return;
        }
        for (const auto& name : requested_game_events_)
        {
            std::string error;
            if (!BindGameEventLocked(name, error))
            {
                game_event_error_ = std::move(error);
                KeelCs2_DestroyGameEventListener(game_event_listener_);
                game_event_listener_ = nullptr;
                bound_game_events_.clear();
                return;
            }
        }
    }

    static KeelHookAction LoadEventsFromFile(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto manager = frame.Argument<void*>(0);
        if (adapter && manager && *manager)
        {
            adapter->CaptureGameEventManager(*manager);
        }
        return KH_ACTION_CONTINUE;
    }

    static void GameEventDispatch(void* event, const char* name, void* user_data)
    {
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        if (adapter && event && name && name[0])
        {
            KeelSource2GameEvent payload{
                sizeof(KeelSource2GameEvent),
                0,
                event,
                name
            };
            static_cast<void>(adapter->EmitSource2(KEELS2_SOURCE2_GAME_EVENT, payload));
        }
    }

    static KeelHookAction RegisterLoopMode(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto name = frame.Argument<const char*>(1);
        const auto factory = frame.Argument<void*>(2);
        if (adapter && name && *name && std::strcmp(*name, "game") == 0 && factory && *factory)
        {
            adapter->EnsureFactoryHooks(*factory);
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction UnregisterLoopMode(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto name = frame.Argument<const char*>(1);
        const auto factory = frame.Argument<void*>(2);
        if (adapter && name && *name && std::strcmp(*name, "game") == 0 && factory && *factory)
        {
            std::scoped_lock lock(adapter->source2_mutex_);
            adapter->active_factories_.erase(*factory);
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction CreateLoopMode(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto factory = frame.Argument<void*>(0);
        const auto loop = frame.Result<void*>();
        bool active{};
        if (adapter && factory && *factory)
        {
            std::scoped_lock lock(adapter->source2_mutex_);
            active = adapter->active_factories_.contains(*factory);
        }
        if (active && loop && *loop)
        {
            adapter->EnsureLoopHooks(*loop);
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction DestroyLoopMode(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto factory = frame.Argument<void*>(0);
        const auto loop = frame.Argument<void*>(1);
        if (adapter && factory && loop && *factory && *loop)
        {
            std::scoped_lock lock(adapter->source2_mutex_);
            if (adapter->active_factories_.contains(*factory))
            {
                adapter->initialized_loops_.erase(*loop);
                adapter->active_loops_.erase(*loop);
            }
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction LoopInit(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto loop = frame.Argument<void*>(0);
        const auto key_values = frame.Argument<void*>(1);
        const auto prerequisites = frame.Argument<void*>(2);
        const auto result = frame.Result<bool>();
        bool emit{};
        if (adapter && loop && *loop && result && *result)
        {
            std::scoped_lock lock(adapter->source2_mutex_);
            emit = adapter->active_loops_.contains(*loop) &&
                adapter->initialized_loops_.insert(*loop).second;
        }
        if (emit)
        {
            KeelSource2LevelInit payload{
                sizeof(KeelSource2LevelInit),
                0,
                key_values ? *key_values : nullptr,
                prerequisites ? *prerequisites : nullptr
            };
            static_cast<void>(adapter->EmitSource2(KEELS2_SOURCE2_LEVEL_INIT, payload));
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction LoopShutdown(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto loop = frame.Argument<void*>(0);
        bool emit{};
        if (adapter && loop && *loop)
        {
            std::scoped_lock lock(adapter->source2_mutex_);
            emit = adapter->initialized_loops_.erase(*loop) != 0;
        }
        if (emit)
        {
            KeelSource2LevelShutdown payload{sizeof(KeelSource2LevelShutdown), 0};
            static_cast<void>(adapter->EmitSource2(KEELS2_SOURCE2_LEVEL_SHUTDOWN, payload));
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction ClientConnect(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto slot = frame.Argument<std::int32_t>(1);
        const auto name = frame.Argument<const char*>(2);
        const auto xuid = frame.Argument<std::uint64_t>(3);
        const auto network_id = frame.Argument<const char*>(4);
        const auto unknown = frame.Argument<bool>(5);
        const auto rejection = frame.Argument<void*>(6);
        if (!adapter || !slot || !name || !*name || !xuid || !network_id || !*network_id ||
            !unknown || !rejection)
        {
            return KH_ACTION_CONTINUE;
        }
        std::array<char, KEELS2_SOURCE2_REJECTION_CAPACITY> message{};
        KeelSource2ClientConnect payload{
            sizeof(KeelSource2ClientConnect),
            *slot,
            *xuid,
            *name,
            *network_id,
            *unknown ? KEEL_TRUE : KEEL_FALSE,
            0,
            message.data(),
            static_cast<std::uint32_t>(message.size()),
            0
        };
        if (adapter->EmitSource2(KEELS2_SOURCE2_CLIENT_CONNECT, payload) == KEEL_FALSE)
        {
            message.back() = '\0';
            const auto length = static_cast<std::uint32_t>(std::strlen(message.data()));
            if (*rejection && !KeelCs2_WriteRejectionMessage(*rejection, message.data(), length))
            {
                platform::WriteEngineConsole(
                    "[KeelS2] ClientConnect rejection message could not be written\n");
            }
            static_cast<void>(frame.SetResult(false));
            return KH_ACTION_SUPERSEDE;
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction ClientCommand(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto slot = frame.Argument<std::int32_t>(1);
        const auto command = frame.Argument<const void*>(2);
        if (!adapter || !slot || !command || !*command)
        {
            return KH_ACTION_CONTINUE;
        }
        KeelSource2ClientCommand payload{
            sizeof(KeelSource2ClientCommand),
            *slot,
            *command
        };
        return adapter->EmitSource2(KEELS2_SOURCE2_CLIENT_COMMAND, payload) == KEEL_FALSE
            ? KH_ACTION_SUPERSEDE
            : KH_ACTION_CONTINUE;
    }

    static bool ValidCompatibility(const KeelHostCompatibilityInfo& compatibility)
    {
#if defined(_WIN32)
        constexpr const char* server_module = "server.dll";
        constexpr const char* engine_module = "engine2.dll";
        constexpr const char* fixture_engine_module = "keels2_bootstrap_integration.exe";
        constexpr std::uint32_t game_event_load_events_slot = 1;
        constexpr std::uint32_t game_event_add_listener_slot = 3;
#else
        constexpr const char* server_module = "libserver.so";
        constexpr const char* engine_module = "libengine2.so";
        constexpr const char* fixture_engine_module = "keels2_bootstrap_integration";
        constexpr std::uint32_t game_event_load_events_slot = 2;
        constexpr std::uint32_t game_event_add_listener_slot = 4;
#endif
        const bool fixture_profile = compatibility.profile &&
            (std::strcmp(compatibility.profile, "test-fixture-linuxsteamrt64") == 0 ||
                std::strcmp(compatibility.profile, "test-fixture-win64") == 0);
        const char* expected_engine_module = fixture_profile
            ? fixture_engine_module
            : engine_module;
        return compatibility.profile && compatibility.profile[0] &&
            compatibility.server_interface &&
            std::strcmp(compatibility.server_interface, "Source2Server001") == 0 &&
            compatibility.server_module &&
            std::strcmp(compatibility.server_module, server_module) == 0 &&
            compatibility.game_clients_interface &&
            std::strcmp(compatibility.game_clients_interface, "Source2GameClients001") == 0 &&
            compatibility.cvar_interface &&
            std::strcmp(compatibility.cvar_interface, cs2::kCvarInterfaceVersion) == 0 &&
            compatibility.cvar_module && compatibility.cvar_module[0] &&
            compatibility.server_validation_slot == 3 &&
            compatibility.game_clients_validation_slot == 0 &&
            compatibility.game_frame_slot == 19 &&
            compatibility.client_connected_slot == 11 &&
            compatibility.client_put_in_server_slot == 13 &&
            compatibility.client_active_slot == 14 &&
            compatibility.client_fully_connected_slot == 15 &&
            compatibility.client_disconnecting_slot == 16 &&
            compatibility.client_settings_changed_slot == 19 &&
            compatibility.client_connect_slot == 12 &&
            compatibility.client_command_slot == 17 &&
            compatibility.engine_service_interface &&
            std::strcmp(compatibility.engine_service_interface, "EngineServiceMgr001") == 0 &&
            compatibility.engine_service_module &&
            std::strcmp(compatibility.engine_service_module, expected_engine_module) == 0 &&
            compatibility.game_event_manager_class &&
            std::strcmp(compatibility.game_event_manager_class, "CGameEventManager") == 0 &&
            compatibility.game_event_module &&
            std::strcmp(compatibility.game_event_module, server_module) == 0 &&
            compatibility.engine_service_register_loop_mode_slot == 13 &&
            compatibility.engine_service_unregister_loop_mode_slot == 14 &&
            compatibility.loop_mode_factory_create_slot == 2 &&
            compatibility.loop_mode_factory_destroy_slot == 3 &&
            compatibility.loop_mode_init_slot == 0 &&
            compatibility.loop_mode_shutdown_slot == 1 &&
            compatibility.game_event_load_events_slot == game_event_load_events_slot &&
            compatibility.game_event_add_listener_slot == game_event_add_listener_slot &&
            compatibility.register_command_slot == cs2::kRegisterConCommandSlot &&
            compatibility.unregister_command_slot == cs2::kUnregisterConCommandSlot &&
            compatibility.command_creation_size == sizeof(cs2::CommandCreation) &&
            compatibility.command_callback_info_size == sizeof(cs2::CommandCallbackInfo) &&
            compatibility.completion_callback_info_size == sizeof(cs2::CompletionCallbackInfo) &&
            compatibility.command_ref_size == sizeof(cs2::CommandRef) &&
            compatibility.command_size == cs2::kCommandSize &&
            compatibility.command_argument_count_offset == cs2::kCommandArgumentCountOffset &&
            compatibility.command_argument_values_offset == cs2::kCommandArgumentValuesOffset &&
            compatibility.find_convar_slot == cs2::kFindConVarSlot &&
            compatibility.register_convar_slot == cs2::kRegisterConVarSlot &&
            compatibility.unregister_convar_slot == cs2::kUnregisterConVarSlot &&
            compatibility.get_convar_data_slot == cs2::kGetConVarDataSlot &&
            compatibility.call_convar_change_slot == cs2::kCallChangeCallbackSlot &&
            compatibility.call_convar_filter_slot == cs2::kCallFilterCallbackSlot &&
            compatibility.call_global_convar_change_slot ==
                cs2::kCallGlobalChangeCallbacksSlot &&
            compatibility.queue_thread_set_value_slot == cs2::kQueueThreadSetValueSlot &&
            compatibility.convar_value_size == sizeof(cs2::ConVarValue) &&
            compatibility.convar_value_info_size == sizeof(cs2::ConVarValueInfo) &&
            compatibility.convar_creation_size == sizeof(cs2::ConVarCreation) &&
            compatibility.convar_ref_size == sizeof(cs2::ConVarRef) &&
            compatibility.convar_data_size == sizeof(cs2::ConVarData) &&
            compatibility.convar_object_size == sizeof(cs2::ConVarObject) &&
            compatibility.convar_data_type_offset == cs2::kConVarDataTypeOffset &&
            compatibility.convar_data_flags_offset == cs2::kConVarDataFlagsOffset &&
            compatibility.convar_data_value_offset == cs2::kConVarDataValueOffset &&
            compatibility.convar_value_info_change_provider_offset ==
                cs2::kConVarValueInfoChangeProviderOffset &&
            compatibility.convar_value_info_custom_data_offset ==
                cs2::kConVarValueInfoCustomDataOffset &&
            compatibility.convar_data_custom_data_offset == cs2::kConVarDataCustomDataOffset &&
            compatibility.convar_object_data_offset == cs2::kConVarObjectDataOffset &&
            compatibility.convar_bool_type == static_cast<std::int32_t>(cs2::ConVarType::boolean) &&
            compatibility.convar_int32_type == static_cast<std::int32_t>(cs2::ConVarType::int32) &&
            compatibility.convar_float32_type == static_cast<std::int32_t>(cs2::ConVarType::float32) &&
            compatibility.convar_string_type == static_cast<std::int32_t>(cs2::ConVarType::string);
    }

    KeelResult DescribeInterface(
        const InterfaceEntry& entry,
        KeelSource2InterfaceInfo& info) const noexcept
    {
        if (!entry.instance || entry.name.empty() || entry.module.empty() ||
            entry.module_path.empty() || compatibility_profile_.empty())
        {
            return KEEL_RESULT_NOT_READY;
        }
        info = {
            sizeof(KeelSource2InterfaceInfo),
            entry.capability,
            entry.factory,
            KEELS2_SOURCE2_OWNERSHIP_BORROWED,
            KEELS2_SOURCE2_LIFETIME_HOST,
            0,
            entry.instance,
            entry.name.c_str(),
            entry.module.c_str(),
            entry.module_path.c_str(),
            compatibility_profile_.c_str()
        };
        return KEEL_RESULT_OK;
    }

    static bool ResolveInterface(
        KeelCreateInterfaceFn factory,
        KeelSource2Capability capability,
        KeelSource2Factory factory_type,
        const char* name,
        const char* expected_module,
        std::uint32_t validation_slot,
        InterfaceEntry& entry,
        std::string& error)
    {
        int return_code = 1;
        void* instance = factory(name, &return_code);
        if (!instance || return_code != 0)
        {
            error = std::string(name) + " is unavailable";
            return false;
        }
        auto** vtable = *reinterpret_cast<void***>(instance);
        if (!vtable)
        {
            error = std::string(name) + " has a null vtable";
            return false;
        }
        std::filesystem::path module;
        if (!ValidateTarget(
                vtable[validation_slot],
                expected_module,
                name,
                module,
                error))
        {
            return false;
        }
        entry.capability = capability;
        entry.factory = factory_type;
        entry.instance = instance;
        entry.name = name;
        entry.module = expected_module;
        entry.module_path = module.string();
        return true;
    }

    static bool ValidInterfaceName(const char* interface_name) noexcept
    {
        if (!interface_name)
        {
            return false;
        }
        for (std::size_t index{}; index < 256; ++index)
        {
            const unsigned char character =
                static_cast<unsigned char>(interface_name[index]);
            if (character == 0)
            {
                return index != 0;
            }
            if (!std::isalnum(character) && character != '_')
            {
                return false;
            }
        }
        return false;
    }

    static bool ValidateTarget(
        void* address,
        const char* expected_module,
        const char* operation,
        std::filesystem::path& module,
        std::string& error)
    {
        if (!address)
        {
            error = std::string(operation) + " validation target is null";
            return false;
        }
        if (!platform::ModulePathFromAddress(address, module, error))
        {
            error = std::string(operation) + " module could not be resolved: " + error;
            return false;
        }

        std::string actual = module.filename().string();
        std::string expected = expected_module;
#if defined(_WIN32)
        std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
#endif
        if (actual != expected)
        {
            error = std::string(operation) + " resolves to unexpected module " + module.string();
            return false;
        }
        return true;
    }

    static bool SameModule(
        const std::filesystem::path& first,
        const std::filesystem::path& second,
        const char* operation,
        std::string& error)
    {
        std::error_code filesystem_error;
        if (!std::filesystem::equivalent(first, second, filesystem_error) || filesystem_error)
        {
            error = std::string(operation) + " validation targets resolve to different modules";
            if (filesystem_error)
            {
                error += ": " + filesystem_error.message();
            }
            return false;
        }
        return true;
    }

    static bool ValidateLifecycleSlot(
        const InterfaceEntry& interface,
        std::uint32_t slot,
        const char* operation,
        std::string& error)
    {
        auto** vtable = *reinterpret_cast<void***>(interface.instance);
        std::filesystem::path module;
        return ValidateTarget(vtable[slot], interface.module.c_str(), operation, module, error) &&
            SameModule(interface.module_path, module, operation, error);
    }

    static bool ValidLifecycleEvent(KeelLifecycleEventType event) noexcept
    {
        return event >= KEELS2_LIFECYCLE_GAME_FRAME &&
            event <= KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED;
    }

    bool AnyLifecycleHook() const noexcept
    {
        return std::any_of(lifecycle_hooks_.begin(), lifecycle_hooks_.end(), [](const LifecycleHook& hook) {
            return hook.callback != 0;
        });
    }

    template <typename Payload>
    void Emit(KeelLifecycleEventType type, const Payload& payload)
    {
        if (!lifecycle_callback_)
        {
            return;
        }
        const KeelLifecycleEvent event{
            sizeof(KeelLifecycleEvent),
            type,
            sizeof(Payload),
            0,
            &payload
        };
        lifecycle_callback_(event, lifecycle_user_data_);
    }

    static KeelHookAction GameFrame(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        if (adapter && !adapter->game_frame_observed_.exchange(true, std::memory_order_acq_rel))
        {
            platform::WriteEngineConsole("[KeelS2] native GameFrame hook entered\n");
        }
        const auto simulating = frame.Argument<bool>(1);
        const auto first_tick = frame.Argument<bool>(2);
        const auto last_tick = frame.Argument<bool>(3);
        if (adapter && simulating && first_tick && last_tick)
        {
            const KeelLifecycleGameFrame payload{
                sizeof(KeelLifecycleGameFrame),
                *simulating ? KEEL_TRUE : KEEL_FALSE,
                *first_tick ? KEEL_TRUE : KEEL_FALSE,
                *last_tick ? KEEL_TRUE : KEEL_FALSE
            };
            adapter->Emit(KEELS2_LIFECYCLE_GAME_FRAME, payload);
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction ClientConnected(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto slot = frame.Argument<std::int32_t>(1);
        const auto name = frame.Argument<const char*>(2);
        const auto xuid = frame.Argument<std::uint64_t>(3);
        const auto network_id = frame.Argument<const char*>(4);
        const auto address = frame.Argument<const char*>(5);
        const auto fake = frame.Argument<bool>(6);
        if (adapter && slot && name && xuid && network_id && address && fake)
        {
            const KeelLifecycleClientConnected payload{
                sizeof(KeelLifecycleClientConnected),
                *slot,
                *xuid,
                *name,
                *network_id,
                *address,
                *fake ? KEEL_TRUE : KEEL_FALSE,
                0
            };
            adapter->Emit(KEELS2_LIFECYCLE_CLIENT_CONNECTED, payload);
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction ClientPutInServer(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto slot = frame.Argument<std::int32_t>(1);
        const auto name = frame.Argument<const char*>(2);
        const auto type = frame.Argument<std::int32_t>(3);
        const auto xuid = frame.Argument<std::uint64_t>(4);
        if (adapter && slot && name && type && xuid)
        {
            const KeelLifecycleClientPutInServer payload{
                sizeof(KeelLifecycleClientPutInServer),
                *slot,
                *xuid,
                *name,
                *type,
                0
            };
            adapter->Emit(KEELS2_LIFECYCLE_CLIENT_PUT_IN_SERVER, payload);
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction ClientActive(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto slot = frame.Argument<std::int32_t>(1);
        const auto load_game = frame.Argument<bool>(2);
        const auto name = frame.Argument<const char*>(3);
        const auto xuid = frame.Argument<std::uint64_t>(4);
        if (adapter && slot && load_game && name && xuid)
        {
            const KeelLifecycleClientActive payload{
                sizeof(KeelLifecycleClientActive),
                *slot,
                *xuid,
                *name,
                *load_game ? KEEL_TRUE : KEEL_FALSE,
                0
            };
            adapter->Emit(KEELS2_LIFECYCLE_CLIENT_ACTIVE, payload);
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction ClientFullyConnected(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto slot = frame.Argument<std::int32_t>(1);
        if (adapter && slot)
        {
            const KeelLifecycleClientFullyConnected payload{
                sizeof(KeelLifecycleClientFullyConnected),
                *slot
            };
            adapter->Emit(KEELS2_LIFECYCLE_CLIENT_FULLY_CONNECTED, payload);
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction ClientDisconnecting(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto slot = frame.Argument<std::int32_t>(1);
        const auto reason = frame.Argument<std::int32_t>(2);
        const auto name = frame.Argument<const char*>(3);
        const auto xuid = frame.Argument<std::uint64_t>(4);
        const auto network_id = frame.Argument<const char*>(5);
        if (adapter && slot && reason && name && xuid && network_id)
        {
            const KeelLifecycleClientDisconnecting payload{
                sizeof(KeelLifecycleClientDisconnecting),
                *slot,
                *xuid,
                *name,
                *network_id,
                *reason,
                0
            };
            adapter->Emit(KEELS2_LIFECYCLE_CLIENT_DISCONNECTING, payload);
        }
        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction ClientSettingsChanged(KeelHookFrame* raw, void* user_data)
    {
        kh::Frame frame(raw);
        auto* adapter = static_cast<Cs2Adapter*>(user_data);
        const auto slot = frame.Argument<std::int32_t>(1);
        if (adapter && slot)
        {
            const KeelLifecycleClientSettingsChanged payload{
                sizeof(KeelLifecycleClientSettingsChanged),
                *slot
            };
            adapter->Emit(KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED, payload);
        }
        return KH_ACTION_CONTINUE;
    }

    InterfaceEntry server_;
    InterfaceEntry game_clients_;
    InterfaceEntry cvar_interface_;
    InterfaceEntry engine_service_;
    KeelCreateInterfaceFn engine_factory_{};
    KeelCreateInterfaceFn server_factory_{};
    std::map<std::pair<KeelSource2Factory, std::string>, InterfaceEntry> named_interfaces_;
    platform::LoadedModulePin game_event_module_pin_;
    void** game_event_manager_vtable_{};
    std::string compatibility_profile_;
    cs2::CvarInterface* cvar_{};
    MemAllocStringDuplicate string_duplicate_{};
    MemAllocFree memory_free_{};
    std::array<LifecycleHook, KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED + 1> lifecycle_hooks_{};
    GameLifecycleCallback lifecycle_callback_{};
    void* lifecycle_user_data_{};
    std::atomic<bool> game_frame_observed_{};
    std::uint32_t game_frame_slot_{};
    std::uint32_t client_connected_slot_{};
    std::uint32_t client_put_in_server_slot_{};
    std::uint32_t client_active_slot_{};
    std::uint32_t client_fully_connected_slot_{};
    std::uint32_t client_disconnecting_slot_{};
    std::uint32_t client_settings_changed_slot_{};
    std::uint32_t client_connect_slot_{};
    std::uint32_t client_command_slot_{};
    std::uint32_t register_loop_mode_slot_{};
    std::uint32_t unregister_loop_mode_slot_{};
    std::uint32_t factory_create_slot_{};
    std::uint32_t factory_destroy_slot_{};
    std::uint32_t loop_init_slot_{};
    std::uint32_t loop_shutdown_slot_{};
    std::uint32_t game_event_load_events_slot_{};
    std::uint32_t game_event_add_listener_slot_{};
    const KeelHookApi* source2_hooks_api_{};
    KeelPluginHandle source2_hook_owner_{};
    GameSource2Callback source2_callback_{};
    void* source2_user_data_{};
    void* game_event_manager_{};
    void* game_event_listener_{};
    std::vector<std::string> requested_game_events_;
    std::unordered_set<std::string> requested_game_event_names_;
    std::unordered_set<std::string> bound_game_events_;
    std::string game_event_error_;
    std::mutex source2_mutex_;
    std::vector<Source2Hook> source2_hooks_;
    std::unordered_set<void*> active_factories_;
    std::unordered_set<void*> active_loops_;
    std::unordered_set<void*> initialized_loops_;
    std::unordered_set<void*> hooked_factory_vtables_;
    std::unordered_set<void*> hooked_loop_vtables_;
    GameConVarHandle next_convar_{1};
    std::unordered_map<GameConVarHandle, std::unique_ptr<ConVarEntry>> convars_;
    std::vector<std::unique_ptr<ConVarEntry>> retired_convars_;
    GameCommandHandle next_command_{1};
    std::unordered_map<GameCommandHandle, std::unique_ptr<CommandEntry>> commands_;
    std::vector<std::unique_ptr<CommandEntry>> retired_commands_;
};

std::unique_ptr<GameAdapter> CreateGameAdapter()
{
    return std::make_unique<Cs2Adapter>();
}

}
