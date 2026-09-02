#ifndef KEELS2_HOST_HOST_H
#define KEELS2_HOST_HOST_H

#include "game_adapter.h"

#include <keels2/bootstrap_api.h>
#include <keels2/platform/dynamic_library.h>
#include <keels2/plugin.h>
#include <keels2/plugins.h>
#include <keels2/source2_authoring.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace keels2::host
{

class KeelHookService;
class GameAdapterModule;
class LifecycleService;
class ConVarService;
class PluginService;
class SchemaEntityService;
class Source2CallbacksService;
class Source2RuntimeService;
class PublishedServiceRegistry;

inline constexpr const char* kHostVersion = "0.9.0";

#if defined(_WIN32)
inline constexpr const char* kPlatformName = "win64";
inline constexpr const char* kPluginExtension = ".dll";
#else
inline constexpr const char* kPlatformName = "linuxsteamrt64";
inline constexpr const char* kPluginExtension = ".so";
#endif

enum class PluginState
{
    loading,
    loaded,
    paused,
    invalid,
    error
};

struct PluginDependencyRecord
{
    std::string name;
    std::string version;
    KeelPluginDependencyRequirement requirement{};
};

struct CompatibilityTargetRecord
{
    std::string name;
    std::string module;
    std::string pattern;
    std::int64_t offset{};
    std::uint32_t occurrence{};
};

struct PluginRecord
{
    KeelPluginHandle handle{};
    platform::DynamicLibrary library;
    KeelPluginLoadFn load{};
    KeelPluginUnloadFn unload{};
    std::filesystem::path path;
    std::filesystem::path transient_path;
    std::string name;
    std::string author;
    std::string version;
    std::string description;
    std::string diagnostic;
    std::vector<PluginDependencyRecord> dependencies;
    PluginState state{PluginState::error};
    bool selectable{};
    bool accepting_resources{};
    bool loading{};
    bool transitioning{};
    bool cleanup_pending{};
};

struct CommandRecord
{
    KeelCommandHandle handle{};
    KeelPluginHandle owner{};
    GameCommandHandle game_handle{};
    std::string name;
    std::string description;
    KeelCommandCallback callback{};
    KeelSource2CommandCallback native_callback{};
    void* user_data{};
    std::atomic<bool> enabled{true};
};

enum class HostState
{
    stopped,
    starting,
    running,
    stopping
};

class Host
{
public:
    static Host& Instance();
    std::uint32_t Start(const KeelHostStartInfo& info);
    bool CompleteStartup();
    bool Stop();
    bool CommandDispatchOpen() const noexcept;

private:
    friend class KeelHookService;
    friend class LifecycleService;
    friend class ConVarService;
    friend class PluginService;
    friend class SchemaEntityService;
    friend class Source2CallbacksService;
    friend class Source2RuntimeService;
    friend class PublishedServiceRegistry;

    Host() = default;
    ~Host();

    bool ReleaseResources(std::unique_lock<std::recursive_mutex>& state_lock);
    bool RegisterCoreCommand();
    void DispatchCoreCommand(
        const KeelCommandInvocation& invocation,
        std::unique_lock<std::recursive_mutex>& state_lock);
    void ShowMainMenu();
    void ShowPluginsMenu();
    void ShowVersion();
    void ShowGame();
    void ShowStatus();
    void ShowCredits();
    void ShowInspectionMenu();
    void ShowHookInspection();
    void ShowInterfaceInspection();
    void ShowServiceInspection();
    void ShowResourceInspection();
    void ShowProfileInspection();
    void ShowPluginList();
    void ShowPluginInfo(std::string_view selector);
    void ShowPluginCommands(const PluginRecord* plugin);
    void LoadPluginCommand(
        std::string_view filename,
        std::unique_lock<std::recursive_mutex>& state_lock);
    bool UnloadPluginCommand(
        std::string_view selector,
        std::unique_lock<std::recursive_mutex>& state_lock);
    void ReloadPluginCommand(
        std::string_view selector,
        std::unique_lock<std::recursive_mutex>& state_lock);
    void RetryPluginCommand(
        std::string_view selector,
        std::unique_lock<std::recursive_mutex>& state_lock);
    void PausePluginCommand(
        std::string_view selector,
        std::unique_lock<std::recursive_mutex>& state_lock);
    void ResumePluginCommand(
        std::string_view selector,
        std::unique_lock<std::recursive_mutex>& state_lock);
    KeelResult PausePlugin(
        KeelPluginHandle target,
        std::unique_lock<std::recursive_mutex>& state_lock,
        bool report);
    KeelResult ResumePlugin(
        KeelPluginHandle target,
        std::unique_lock<std::recursive_mutex>& state_lock,
        bool report);
    void RestorePluginDispatch(PluginRecord& plugin);
    void PublishPluginEvent(
        KeelPluginEventType event,
        const PluginRecord& plugin,
        std::unique_lock<std::recursive_mutex>& state_lock);
    void FillPluginSnapshot(const PluginRecord& plugin, KeelPluginSnapshot& snapshot) const noexcept;

    void LoadPlugins(
        const std::filesystem::path& directory,
        std::unique_lock<std::recursive_mutex>& state_lock);
    PluginRecord* LoadPlugin(
        const std::filesystem::path& path,
        std::unique_lock<std::recursive_mutex>& state_lock);
    PluginRecord* DiscoverPlugin(
        const std::filesystem::path& path,
        std::unique_lock<std::recursive_mutex>& state_lock);
    PluginRecord* StartPlugin(
        PluginRecord& record,
        std::unique_lock<std::recursive_mutex>& state_lock);
    bool DependenciesReady(const PluginRecord& plugin, std::string& diagnostic) const;
    bool HasRunningDependent(const PluginRecord& plugin, std::string& dependent) const;
    void RejectUnstartedPlugin(PluginRecord& plugin, std::string diagnostic);
    bool ResolvePluginPath(std::string_view filename, std::filesystem::path& path);
    void RemoveCommandsOwnedBy(KeelPluginHandle owner);
    void SetCommandsOwnedEnabled(KeelPluginHandle owner, bool enabled);
    void ClosePluginImage(PluginRecord& plugin) noexcept;
    void RemovePluginRecord(KeelPluginHandle handle);
    PluginRecord* PluginByHandle(KeelPluginHandle handle);
    const PluginRecord* PluginByHandle(KeelPluginHandle handle) const;
    PluginRecord* SelectPlugin(std::string_view selector);
    std::size_t PluginDisplayIndex(const PluginRecord* plugin) const;
    std::string PluginDisplayId(const PluginRecord* plugin) const;
    std::size_t PluginCommandCount(KeelPluginHandle owner) const;
    std::string ResourceOwnerLabel(KeelPluginHandle owner) const;

    void PluginLog(KeelPluginHandle plugin, KeelLogLevel level, const char* message);
    KeelResult RegisterCommand(
        KeelPluginHandle plugin,
        const KeelCommandSpec* spec,
        KeelCommandHandle* output);
    KeelResult RegisterSource2Command(
        KeelPluginHandle plugin,
        const KeelSource2CommandSpec* spec,
        KeelCommandHandle* output);
    KeelResult UnregisterCommand(KeelPluginHandle plugin, KeelCommandHandle command);
    KeelResult QueryService(
        KeelPluginHandle plugin,
        const char* name,
        std::uint32_t version,
        const void** service);
    KeelResult QuerySource2Interface(
        KeelPluginHandle plugin,
        KeelSource2Capability capability,
        KeelSource2InterfaceInfo* info);
    KeelResult QuerySource2NamedInterface(
        KeelPluginHandle plugin,
        KeelSource2Factory factory,
        const char* interface_name,
        KeelSource2InterfaceInfo* info);
    bool RegisterCommandRecord(std::unique_ptr<CommandRecord> resource, std::uint64_t flags);

    static void ApiLog(KeelPluginHandle plugin, KeelLogLevel level, const char* message);
    static KeelResult ApiRegisterCommand(
        KeelPluginHandle plugin,
        const KeelCommandSpec* spec,
        KeelCommandHandle* command);
    static KeelResult ApiUnregisterCommand(
        KeelPluginHandle plugin,
        KeelCommandHandle command);
    static KeelResult ApiRegisterSource2Command(
        KeelPluginHandle plugin,
        const KeelSource2CommandSpec* spec,
        KeelCommandHandle* command);
    static KeelResult ApiUnregisterSource2Command(
        KeelPluginHandle plugin,
        KeelCommandHandle command);
    static KeelResult ApiCreateSource2ConVar(
        KeelPluginHandle plugin,
        const KeelConVarSpec* spec,
        KeelSource2ConVarChangeCallback callback,
        void* user_data,
        KeelConVarHandle* convar,
        void** native_convar);
    static KeelResult ApiFindSource2ConVar(
        KeelPluginHandle plugin,
        const char* name,
        KeelConVarType expected_type,
        KeelConVarHandle* convar,
        void** native_convar);
    static KeelResult ApiReleaseSource2ConVar(
        KeelPluginHandle plugin,
        KeelConVarHandle convar);
    static KeelResult ApiQueryService(
        KeelPluginHandle plugin,
        const char* name,
        std::uint32_t version,
        const void** service);
    static KeelResult ApiQuerySource2Interface(
        KeelPluginHandle plugin,
        KeelSource2Capability capability,
        KeelSource2InterfaceInfo* info);
    static KeelResult ApiQuerySource2NamedInterface(
        KeelPluginHandle plugin,
        KeelSource2Factory factory,
        const char* interface_name,
        KeelSource2InterfaceInfo* info);
    static void DispatchCommand(const GameCommandInvocation& invocation, void* user_data);
    static void CoreCommand(const KeelCommandInvocation* invocation, void* user_data);

    static bool ValidCommandName(const char* name);
    static bool ValidMetadataText(const char* text, std::size_t maximum, bool allow_empty);
    static bool ValidPluginName(const char* name);
    static bool EqualInsensitive(std::string_view left, std::string_view right);
    static bool StartsWithInsensitive(std::string_view text, std::string_view prefix);
    static bool ContainsInsensitive(std::string_view text, std::string_view part);
    static std::string FormatPluginIndex(std::size_t index);
    static const char* PluginStateLabel(PluginState state);
    static KeelPluginRuntimeState PublicPluginState(PluginState state) noexcept;
    static bool ParseSemanticVersion(
        std::string_view version,
        std::array<std::uint32_t, 3>& output) noexcept;
    static bool DependencyVersionMatches(
        std::string_view actual,
        const PluginDependencyRecord& dependency) noexcept;

    void WriteShutdownTrace(std::string_view event, std::string_view detail = {}) noexcept;
    void Write(KeelLogLevel level, const std::string& message);
    void WriteUsage(const std::string& usage);
    void WriteLine(const std::string& message);

    std::mutex lifecycle_mutex_;
    std::recursive_mutex state_mutex_;
    std::mutex log_mutex_;
    HostState state_{HostState::stopped};
    bool accepting_resources_{};
    bool cleanup_failure_reported_{};
    bool lifecycle_failure_reported_{};
    bool convar_failure_reported_{};
    bool plugin_service_failure_reported_{};
    bool plugin_transition_failure_reported_{};
    bool source2_callbacks_failure_reported_{};
    std::atomic<bool> dispatch_open_{false};
    std::filesystem::path bootstrap_directory_;
    std::filesystem::path plugin_directory_;
    std::string game_;
    std::string platform_;
    std::string game_version_;
    std::string compatibility_profile_;
    std::vector<CompatibilityTargetRecord> compatibility_targets_;
    std::unique_ptr<GameAdapterModule> adapter_module_;
    GameAdapter* adapter_{};
    std::unique_ptr<KeelHookService> keelhook_;
    std::unique_ptr<LifecycleService> lifecycle_;
    std::unique_ptr<ConVarService> convars_;
    std::unique_ptr<PluginService> plugin_service_;
    std::unique_ptr<SchemaEntityService> schema_entities_;
    std::unique_ptr<Source2CallbacksService> source2_callbacks_;
    std::unique_ptr<Source2RuntimeService> source2_runtime_;
    std::unique_ptr<PublishedServiceRegistry> published_services_;
    KeelSource2ApiV1 source2_api_v1_{};
    KeelSource2Api source2_api_{};
    KeelSource2AuthoringApi source2_authoring_api_{};
    KeelHostApi api_{};
    KeelPluginHandle next_plugin_{1};
    KeelCommandHandle next_command_{1};
    std::vector<std::unique_ptr<PluginRecord>> plugins_;
    std::unordered_map<KeelCommandHandle, std::unique_ptr<CommandRecord>> commands_;
    std::vector<std::unique_ptr<CommandRecord>> retired_commands_;
    std::vector<KeelPluginHandle> load_order_;
};

std::uint32_t BeginGameCommandDispatch() noexcept;
void EndGameCommandDispatch() noexcept;

}

#endif
