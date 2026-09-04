#include <keels2/bootstrap_api.h>
#include <keels2/cs2/compatibility.h>
#include <keels2/platform/console.h>
#include <keels2/platform/diagnostic_trace.h>
#include <keels2/platform/dynamic_library.h>
#include <keels2/platform/file_fingerprint.h>

#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#define KEELS2_BOOTSTRAP_EXPORT __declspec(dllexport)
#else
#include <sys/mman.h>
#include <unistd.h>
#define KEELS2_BOOTSTRAP_EXPORT __attribute__((visibility("default")))
#endif

namespace keels2::bootstrap
{

#if defined(_WIN32)
inline constexpr const char* kPlatformName = "win64";
#else
inline constexpr const char* kPlatformName = "linuxsteamrt64";
#endif

template <typename Function>
void* FunctionAddress(Function function)
{
    static_assert(std::is_pointer_v<Function>);
    static_assert(sizeof(Function) == sizeof(void*));
    void* address{};
    std::memcpy(&address, &function, sizeof(address));
    return address;
}

template <typename Function>
Function AddressFunction(void* address)
{
    static_assert(std::is_pointer_v<Function>);
    static_assert(sizeof(Function) == sizeof(void*));
    Function function{};
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

struct VtableWriteResult
{
    bool written{};
    bool protection_restored{};
};

static VtableWriteResult WriteVtablePointer(void** slot, void* value, std::string& error)
{
#if defined(_WIN32)
    DWORD previous_protection{};
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous_protection))
    {
        error = "VirtualProtect failed with error " + std::to_string(GetLastError());
        return {};
    }
    *slot = value;
    DWORD ignored{};
    if (!VirtualProtect(slot, sizeof(void*), previous_protection, &ignored))
    {
        error = "VirtualProtect could not restore page protection";
        return {true, false};
    }
#else
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
    {
        error = "sysconf could not determine the page size";
        return {};
    }
    const auto address = reinterpret_cast<std::uintptr_t>(slot);
    const auto page = address & ~(static_cast<std::uintptr_t>(page_size) - 1u);
    if (mprotect(reinterpret_cast<void*>(page), static_cast<std::size_t>(page_size), PROT_READ | PROT_WRITE) != 0)
    {
        error = "mprotect failed: " + std::string(std::strerror(errno));
        return {};
    }
    *slot = value;
    if (mprotect(reinterpret_cast<void*>(page), static_cast<std::size_t>(page_size), PROT_READ) != 0)
    {
        error = "mprotect could not restore page protection: " + std::string(std::strerror(errno));
        return {true, false};
    }
#endif

    error.clear();
    return {true, true};
}

class VtablePatch
{
public:
    bool Install(
        void* object,
        std::size_t index,
        void* replacement,
        const std::filesystem::path& expected_module,
        std::string& error)
    {
        if (!object || !replacement)
        {
            error = "vtable patch received a null address";
            return false;
        }

        auto** vtable = *static_cast<void***>(object);
        void** requested_slot = &vtable[index];
        if (installed_)
        {
            if (slot_ == requested_slot)
            {
                error.clear();
                return true;
            }
            error = "vtable patch is already installed on a different object";
            return false;
        }

        slot_ = requested_slot;
        original_ = *slot_;
        if (!original_)
        {
            error = "vtable entry is null";
            slot_ = nullptr;
            return false;
        }

        std::filesystem::path original_module;
        if (!platform::ModulePathFromAddress(original_, original_module, error))
        {
            slot_ = nullptr;
            original_ = nullptr;
            return false;
        }
        std::error_code filesystem_error;
        if (!std::filesystem::equivalent(original_module, expected_module, filesystem_error) || filesystem_error)
        {
            error = "vtable entry resolves outside the genuine server module: " + original_module.string();
            if (filesystem_error)
            {
                error += ": " + filesystem_error.message();
            }
            slot_ = nullptr;
            original_ = nullptr;
            return false;
        }

        const VtableWriteResult result = WriteVtablePointer(slot_, replacement, error);
        if (!result.written)
        {
            slot_ = nullptr;
            original_ = nullptr;
            return false;
        }
        installed_ = true;
        return result.protection_restored;
    }

    bool Restore(std::string& error)
    {
        if (!installed_)
        {
            return true;
        }
        const VtableWriteResult result = WriteVtablePointer(slot_, original_, error);
        if (!result.written)
        {
            return false;
        }
        installed_ = false;
        slot_ = nullptr;
        return result.protection_restored;
    }

    template <typename Function>
    Function Original() const
    {
        return AddressFunction<Function>(original_);
    }

private:
    void** slot_{};
    void* original_{};
    bool installed_{};
};

enum class HostState
{
    stopped,
    starting,
    running,
    stopping
};

class Bootstrap
{
public:
    using ConnectFn = bool (*)(void* self, KeelCreateInterfaceFn factory);
    using DisconnectFn = void (*)(void* self);
    using InitFn = int (*)(void* self);

    static Bootstrap& Instance()
    {
        static Bootstrap* instance = new Bootstrap();
        return *instance;
    }

    void* QueryInterface(const char* name, int* return_code)
    {
        if (!name)
        {
            if (return_code)
            {
                *return_code = 1;
            }
            return nullptr;
        }

        std::scoped_lock lock(mutex_);
        if (!EnsureRealServer())
        {
            if (return_code)
            {
                *return_code = 1;
            }
            return nullptr;
        }

        void* interface_pointer = server_factory_(name, return_code);
        if (!interface_pointer)
        {
            return nullptr;
        }

        if (profile_ && !capture_disabled_ && !lifecycle_complete_ &&
            std::strcmp(name, profile_->server_config_interface) == 0)
        {
            PatchConfig(interface_pointer);
        }
        else if (profile_ && !capture_disabled_ && !lifecycle_complete_ &&
            std::strcmp(name, profile_->server_interface) == 0)
        {
            PatchServer(interface_pointer);
        }
        return interface_pointer;
    }

    bool OnConnect(void* self, KeelCreateInterfaceFn factory)
    {
        ConnectFn original{};
        {
            std::scoped_lock lock(mutex_);
            engine_factory_ = factory;
            connect_observed_ = true;
            original = connect_patch_.Original<ConnectFn>();
            RestorePatch(connect_patch_, "Source2ServerConfig::Connect");
        }
        return original ? original(self, factory) : false;
    }

    void OnDisconnect(void* self)
    {
        platform::AppendShutdownTrace("bootstrap disconnect entered");
        DisconnectFn original{};
        {
            std::scoped_lock lock(mutex_);
            platform::AppendShutdownTrace("bootstrap disconnect lock acquired");
            original = disconnect_patch_.Original<DisconnectFn>();
            platform::AppendShutdownTrace("bootstrap disconnect patch restoration begin");
            RestorePatch(disconnect_patch_, "Source2ServerConfig::Disconnect");
            platform::AppendShutdownTrace("bootstrap disconnect patch restoration complete");
            platform::AppendShutdownTrace("bootstrap host stop begin");
            StopHost();
            platform::AppendShutdownTrace("bootstrap host stop returned");
            lifecycle_complete_ = true;
            platform::AppendShutdownTrace("bootstrap lifecycle marked complete");
        }
        if (original)
        {
            platform::AppendShutdownTrace("bootstrap genuine disconnect begin");
            original(self);
            platform::AppendShutdownTrace("bootstrap genuine disconnect complete");
        }
        else
        {
            platform::AppendShutdownTrace("bootstrap genuine disconnect unavailable");
        }
        platform::AppendShutdownTrace("bootstrap disconnect complete");
    }

    int OnInit(void* self)
    {
        InitFn original{};
        {
            std::scoped_lock lock(mutex_);
            init_observed_ = true;
            original = init_patch_.Original<InitFn>();
            RestorePatch(init_patch_, "Source2Server::Init");
            StartHost();
        }

        const int result = original ? original(self) : 0;
        if (result == 0)
        {
            std::scoped_lock lock(mutex_);
            StopHost();
            return 0;
        }
        std::scoped_lock lock(mutex_);
        if (host_state_ == HostState::running &&
            (!host_complete_startup_ || host_complete_startup_() == 0))
        {
            Log("host rejected post-init completion");
            StopHost();
            return 0;
        }
        return result;
    }

private:
    static bool ConnectHook(void* self, KeelCreateInterfaceFn factory)
    {
        return Instance().OnConnect(self, factory);
    }

    static void DisconnectHook(void* self)
    {
        Instance().OnDisconnect(self);
    }

    static int InitHook(void* self)
    {
        return Instance().OnInit(self);
    }

    bool EnsureRealServer()
    {
        if (server_factory_)
        {
            return true;
        }
        if (real_server_attempted_)
        {
            return false;
        }
        real_server_attempted_ = true;

        std::filesystem::path module_path;
        std::string error;
        if (!platform::ModulePathFromAddress(FunctionAddress(&ConnectHook), module_path, error))
        {
            Log("could not locate bootstrap module: " + error);
            return false;
        }

        std::error_code filesystem_error;
        module_path = std::filesystem::weakly_canonical(module_path, filesystem_error);
        if (filesystem_error)
        {
            Log("could not canonicalize bootstrap path: " + filesystem_error.message());
            return false;
        }

        bootstrap_directory_ = module_path.parent_path();
        std::filesystem::path csgo_directory = bootstrap_directory_;
        for (int index = 0; index < 4; ++index)
        {
            csgo_directory = csgo_directory.parent_path();
        }

#if defined(_WIN32)
        const char* real_server_name = "server.dll";
#else
        const char* real_server_name = "libserver.so";
#endif
        const auto real_server_path = csgo_directory / "bin" / bootstrap_directory_.filename() / real_server_name;
        if (!std::filesystem::is_regular_file(real_server_path, filesystem_error) || filesystem_error)
        {
            Log("genuine server module is unavailable: " + real_server_path.string());
            return false;
        }
        if (std::filesystem::equivalent(module_path, real_server_path, filesystem_error) && !filesystem_error)
        {
            Log("genuine server path resolves to the KeelS2 proxy");
            return false;
        }

        if (!real_server_.Open(real_server_path, error))
        {
            Log("could not load genuine server module: " + error);
            return false;
        }
        server_factory_ = AddressFunction<KeelCreateInterfaceFn>(real_server_.Symbol("CreateInterface"));
        if (!server_factory_)
        {
            Log("genuine server module does not export CreateInterface");
            real_server_.Close();
            return false;
        }

        platform::FileFingerprint fingerprint;
        if (!platform::FingerprintFile(real_server_path, fingerprint, error))
        {
            Log("could not fingerprint genuine server module: " + error);
        }
        else
        {
            profile_ = cs2::FindCompatibilityProfile(fingerprint, kPlatformName);
        }

#if defined(KEELS2_TEST_COMPATIBILITY)
        if (!profile_ && real_server_.Symbol("KeelTest_CompatibilityMarker"))
        {
            profile_ = &cs2::FixtureCompatibilityProfile(kPlatformName);
        }
#endif

        bootstrap_directory_text_ = bootstrap_directory_.string();
        real_server_path_ = real_server_path;
        Log("loaded genuine server module: " + real_server_path.string());
        if (profile_)
        {
            Log("selected compatibility profile: " + std::string(profile_->id));
        }
        else
        {
            const std::string identity = fingerprint.size == 0
                ? "unavailable"
                : platform::FormatFingerprint(fingerprint);
            Log("unsupported cs2 server module for " + std::string(kPlatformName) + ": " + identity);
            Log("lifecycle capture and plugin loading are disabled; genuine interfaces remain available");
        }
        return true;
    }

    void PatchConfig(void* interface_pointer)
    {
        std::string error;
        if (!connect_observed_ && !connect_patch_.Install(
                interface_pointer,
                profile_->connect_slot,
                FunctionAddress(&ConnectHook),
                real_server_path_,
                error))
        {
            Log("could not capture Source2ServerConfig::Connect: " + error);
            DisableLifecycleCapture();
            return;
        }
        if (!disconnect_patch_.Install(
                interface_pointer,
                profile_->disconnect_slot,
                FunctionAddress(&DisconnectHook),
                real_server_path_,
                error))
        {
            Log("could not capture Source2ServerConfig::Disconnect: " + error);
            DisableLifecycleCapture();
        }
    }

    void PatchServer(void* interface_pointer)
    {
        if (init_observed_)
        {
            return;
        }
        std::string error;
        if (!init_patch_.Install(
                interface_pointer,
                profile_->init_slot,
                FunctionAddress(&InitHook),
                real_server_path_,
                error))
        {
            Log("could not capture Source2Server::Init: " + error);
            DisableLifecycleCapture();
        }
    }

    void DisableLifecycleCapture()
    {
        if (capture_disabled_)
        {
            return;
        }
        capture_disabled_ = true;
        RestorePatch(connect_patch_, "Source2ServerConfig::Connect");
        RestorePatch(disconnect_patch_, "Source2ServerConfig::Disconnect");
        RestorePatch(init_patch_, "Source2Server::Init");
        Log("lifecycle capture disabled after compatibility validation failure");
    }

    void RestorePatch(VtablePatch& patch, const char* name)
    {
        std::string error;
        if (!patch.Restore(error))
        {
            Log(std::string("could not restore ") + name + ": " + error);
        }
    }

    void StartHost()
    {
        if (host_state_ == HostState::running)
        {
            return;
        }
        if (host_state_ != HostState::stopped)
        {
            Log("host start requested during another lifecycle transition");
            return;
        }
        if (!engine_factory_)
        {
            Log("host was not started because Source2ServerConfig::Connect was not observed");
            return;
        }
        if (!profile_)
        {
            Log("host was not started because no compatibility profile is active");
            return;
        }
        if (capture_disabled_)
        {
            Log("host was not started because lifecycle compatibility validation failed");
            return;
        }
        if (lifecycle_complete_)
        {
            Log("host was not started because the captured lifecycle is complete");
            return;
        }

        host_state_ = HostState::starting;

#if defined(_WIN32)
        const char* host_name = "keels2_host.dll";
#else
        const char* host_name = "libkeels2_host.so";
#endif
        std::string error;
        const auto host_path = bootstrap_directory_ / host_name;
        if (!host_library_.Open(host_path, error))
        {
            Log("could not load host: " + error);
            CloseHostLibrary();
            return;
        }

        host_start_ = AddressFunction<KeelHostStartFn>(host_library_.Symbol("KeelHost_Start"));
        host_complete_startup_ = AddressFunction<KeelHostCompleteStartupFn>(
            host_library_.Symbol("KeelHost_CompleteStartup"));
        host_stop_ = AddressFunction<KeelHostStopFn>(host_library_.Symbol("KeelHost_Stop"));
        if (!host_start_ || !host_complete_startup_ || !host_stop_)
        {
            Log("host exports are incomplete");
            CloseHostLibrary();
            return;
        }

        std::vector<std::string> target_modules;
        target_modules.reserve(profile_->target_count);
        for (std::uint32_t index{}; index < profile_->target_count; ++index)
        {
            const auto& target = profile_->targets[index];
            target_modules.emplace_back(
                std::strcmp(target.module, profile_->server_module) == 0
                    ? real_server_path_.string()
                    : target.module);
        }

        std::vector<KeelHostCompatibilityTargetInfo> targets;
        targets.reserve(profile_->target_count);
        for (std::uint32_t index{}; index < profile_->target_count; ++index)
        {
            const auto& target = profile_->targets[index];
            targets.push_back({
                sizeof(KeelHostCompatibilityTargetInfo),
                target.occurrence,
                target.offset,
                target.name,
                target_modules[index].c_str(),
                target.pattern
            });
        }
        const KeelHostCompatibilityInfo compatibility{
            sizeof(KeelHostCompatibilityInfo),
            profile_->id,
            profile_->game_version,
            profile_->server_interface,
            profile_->server_module,
            profile_->game_clients_interface,
            profile_->cvar_interface,
            profile_->cvar_module,
            profile_->init_slot,
            profile_->game_clients_validation_slot,
            profile_->game_frame_slot,
            profile_->client_connected_slot,
            profile_->client_put_in_server_slot,
            profile_->client_active_slot,
            profile_->client_fully_connected_slot,
            profile_->client_disconnecting_slot,
            profile_->client_settings_changed_slot,
            profile_->register_command_slot,
            profile_->unregister_command_slot,
            profile_->command_creation_size,
            profile_->command_callback_info_size,
            profile_->completion_callback_info_size,
            profile_->command_ref_size,
            profile_->command_size,
            profile_->command_argument_count_offset,
            profile_->command_argument_values_offset,
            profile_->find_convar_slot,
            profile_->register_convar_slot,
            profile_->unregister_convar_slot,
            profile_->get_convar_data_slot,
            profile_->call_convar_change_slot,
            profile_->call_convar_filter_slot,
            profile_->call_global_convar_change_slot,
            profile_->queue_thread_set_value_slot,
            profile_->convar_value_size,
            profile_->convar_value_info_size,
            profile_->convar_creation_size,
            profile_->convar_ref_size,
            profile_->convar_data_size,
            profile_->convar_object_size,
            profile_->convar_data_type_offset,
            profile_->convar_data_flags_offset,
            profile_->convar_data_value_offset,
            profile_->convar_value_info_change_provider_offset,
            profile_->convar_value_info_custom_data_offset,
            profile_->convar_data_custom_data_offset,
            profile_->convar_object_data_offset,
            profile_->convar_bool_type,
            profile_->convar_int32_type,
            profile_->convar_float32_type,
            profile_->convar_string_type,
            profile_->engine_service_interface,
            profile_->engine_service_module,
            profile_->game_event_manager_class,
            profile_->game_event_module,
            profile_->engine_service_register_loop_mode_slot,
            profile_->engine_service_unregister_loop_mode_slot,
            profile_->loop_mode_factory_create_slot,
            profile_->loop_mode_factory_destroy_slot,
            profile_->loop_mode_init_slot,
            profile_->loop_mode_shutdown_slot,
            profile_->game_event_load_events_slot,
            profile_->game_event_add_listener_slot,
            profile_->client_connect_slot,
            profile_->client_command_slot,
            profile_->schema_interface,
            profile_->schema_module,
            profile_->schema_server_module,
            profile_->schema_validation_slot,
            profile_->game_resource_interface,
            profile_->game_resource_module,
            profile_->entity_system_module,
            profile_->game_resource_validation_slot,
            profile_->game_entity_system_offset,
            static_cast<std::uint32_t>(targets.size()),
            0,
            targets.empty() ? nullptr : targets.data()
        };
        const KeelHostStartInfo info{
            sizeof(KeelHostStartInfo),
            KEELS2_HOST_ABI_VERSION,
            engine_factory_,
            server_factory_,
            bootstrap_directory_text_.c_str(),
            "cs2",
            kPlatformName,
            &compatibility
        };
        const std::uint32_t start_result = host_start_(&info);
        if (start_result == KEELS2_HOST_START_FAILED)
        {
            Log("host rejected startup");
            CloseHostLibrary();
            return;
        }
        if (start_result == KEELS2_HOST_START_RETAINED)
        {
            Log("host startup failed and its module was retained because native resources remain active");
            host_state_ = HostState::stopping;
            return;
        }
        if (start_result != KEELS2_HOST_START_RUNNING)
        {
            Log("host returned an unsupported startup result; retaining its module");
            host_state_ = HostState::stopping;
            return;
        }

        host_state_ = HostState::running;
    }

    void StopHost()
    {
        platform::AppendShutdownTrace("bootstrap StopHost entered");
        if (host_state_ != HostState::running && host_state_ != HostState::stopping)
        {
            platform::AppendShutdownTrace("bootstrap StopHost skipped");
            return;
        }
        if (host_state_ == HostState::running)
        {
            host_state_ = HostState::stopping;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool waiting{};
        while (true)
        {
            platform::AppendShutdownTrace("bootstrap host stop export call begin");
            const bool stopped = host_stop_() != 0;
            platform::AppendShutdownTrace(
                stopped
                    ? "bootstrap host stop export reported complete"
                    : "bootstrap host stop export requested retry");
            if (stopped)
            {
                break;
            }
            if (!waiting)
            {
                Log("host cleanup is waiting for native resources to become safe");
                waiting = true;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                Log("host module retained because native resources remain active");
                platform::AppendShutdownTrace("bootstrap host module retained");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        platform::AppendShutdownTrace("bootstrap host library close begin");
        CloseHostLibrary();
        platform::AppendShutdownTrace("bootstrap host library close complete");
        platform::AppendShutdownTrace("bootstrap StopHost complete");
    }

    void CloseHostLibrary()
    {
        host_start_ = nullptr;
        host_complete_startup_ = nullptr;
        host_stop_ = nullptr;
        host_library_.Close();
        host_state_ = HostState::stopped;
    }

    static void Log(const std::string& message)
    {
        const std::string line = "[KeelS2/bootstrap] " + message + "\n";
        platform::WriteEngineConsole(line.c_str());
    }

    std::recursive_mutex mutex_;
    platform::DynamicLibrary real_server_;
    platform::DynamicLibrary host_library_;
    KeelCreateInterfaceFn server_factory_{};
    KeelCreateInterfaceFn engine_factory_{};
    KeelHostStartFn host_start_{};
    KeelHostCompleteStartupFn host_complete_startup_{};
    KeelHostStopFn host_stop_{};
    VtablePatch connect_patch_;
    VtablePatch disconnect_patch_;
    VtablePatch init_patch_;
    std::filesystem::path bootstrap_directory_;
    std::filesystem::path real_server_path_;
    std::string bootstrap_directory_text_;
    const cs2::CompatibilityProfile* profile_{};
    bool connect_observed_{};
    bool init_observed_{};
    bool real_server_attempted_{};
    bool capture_disabled_{};
    bool lifecycle_complete_{};
    HostState host_state_{HostState::stopped};
};

}

extern "C" KEELS2_BOOTSTRAP_EXPORT void* CreateInterface(const char* name, int* return_code)
{
    try
    {
        return keels2::bootstrap::Bootstrap::Instance().QueryInterface(name, return_code);
    }
    catch (...)
    {
        if (return_code)
        {
            *return_code = 1;
        }
        return nullptr;
    }
}
