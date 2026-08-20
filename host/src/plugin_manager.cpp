#include "host.h"
#include "convar_service.h"
#include "keelhook_service.h"
#include "lifecycle_service.h"
#include "plugin_service.h"
#include "source2_callbacks_service.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <system_error>

namespace keels2::host
{

namespace
{

template <std::size_t Capacity>
void CopySnapshotText(char (&destination)[Capacity], std::string_view source) noexcept
{
    static_assert(Capacity != 0);
    const std::size_t length = std::min(source.size(), Capacity - 1);
    if (length != 0)
    {
        std::memcpy(destination, source.data(), length);
    }
    destination[length] = '\0';
}

class PluginTransition final
{
public:
    explicit PluginTransition(PluginRecord& plugin) noexcept
    {
        if (!plugin.transitioning)
        {
            plugin.transitioning = true;
            plugin_ = &plugin;
        }
    }

    ~PluginTransition()
    {
        if (plugin_)
        {
            plugin_->transitioning = false;
        }
    }

    PluginTransition(const PluginTransition&) = delete;
    PluginTransition& operator=(const PluginTransition&) = delete;

    explicit operator bool() const noexcept
    {
        return plugin_ != nullptr;
    }

    void Disarm() noexcept
    {
        plugin_ = nullptr;
    }

private:
    PluginRecord* plugin_{};
};

}

void Host::LoadPlugins(
    const std::filesystem::path& directory,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    std::error_code error;
    if (!std::filesystem::exists(directory, error))
    {
#if defined(NDEBUG)
        WriteLine("[KeelS2] No plugins loaded (none found)");
#else
        Write(KEEL_LOG_WARNING, "no plugins found in: " + directory.string());
#endif
        return;
    }

    std::vector<std::filesystem::path> paths;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end;
         iterator.increment(error))
    {
        if (!iterator->is_regular_file(error))
        {
            continue;
        }
        if (EqualInsensitive(iterator->path().extension().string(), kPluginExtension))
        {
            paths.push_back(iterator->path());
        }
    }
    if (error)
    {
        Write(KEEL_LOG_ERROR, "could not enumerate plugins: " + error.message());
        return;
    }

    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        return left.filename().string() < right.filename().string();
    });
    if (paths.empty())
    {
#if defined(NDEBUG)
        WriteLine("[KeelS2] No plugins loaded (none found)");
#else
        Write(KEEL_LOG_WARNING, "no plugins found in: " + directory.string());
#endif
        return;
    }

    std::vector<PluginRecord*> discovered;
    discovered.reserve(paths.size());
    for (const auto& path : paths)
    {
        if (PluginRecord* plugin = DiscoverPlugin(path, state_lock))
        {
            discovered.push_back(plugin);
        }
    }

    while (true)
    {
        bool remaining{};
        bool progress{};
        for (PluginRecord* plugin : discovered)
        {
            if (!plugin || plugin->state != PluginState::loading)
            {
                continue;
            }
            remaining = true;
            bool waiting{};
            std::string diagnostic;
            for (const auto& dependency : plugin->dependencies)
            {
                const auto target = std::find_if(plugins_.begin(), plugins_.end(), [&](const auto& candidate) {
                    return candidate->selectable &&
                        EqualInsensitive(candidate->name, dependency.name);
                });
                if (target == plugins_.end())
                {
                    diagnostic = "missing dependency " + dependency.name + " " + dependency.version;
                    break;
                }
                if (target->get() == plugin)
                {
                    diagnostic = "plugin cannot depend on itself";
                    break;
                }
                if (!DependencyVersionMatches((*target)->version, dependency))
                {
                    diagnostic = "dependency version mismatch for " + dependency.name +
                        ": found " + (*target)->version + ", required " + dependency.version;
                    break;
                }
                if ((*target)->state == PluginState::loading)
                {
                    waiting = true;
                }
                else if ((*target)->state != PluginState::loaded)
                {
                    diagnostic = "dependency is not running: " + dependency.name;
                    break;
                }
            }
            if (!diagnostic.empty())
            {
                RejectUnstartedPlugin(*plugin, std::move(diagnostic));
                progress = true;
            }
            else if (!waiting)
            {
                StartPlugin(*plugin, state_lock);
                progress = true;
            }
        }
        if (!remaining)
        {
            break;
        }
        if (!progress)
        {
            for (PluginRecord* plugin : discovered)
            {
                if (plugin && plugin->state == PluginState::loading)
                {
                    RejectUnstartedPlugin(*plugin, "dependency cycle detected");
                }
            }
            break;
        }
    }
    if (plugin_service_)
    {
        state_lock.unlock();
        plugin_service_->PublishAllLoaded();
        state_lock.lock();
    }
}

PluginRecord* Host::LoadPlugin(
    const std::filesystem::path& path,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    PluginRecord* record = DiscoverPlugin(path, state_lock);
    if (!record || record->state != PluginState::loading)
    {
        return record;
    }
    std::string diagnostic;
    if (!DependenciesReady(*record, diagnostic))
    {
        RejectUnstartedPlugin(*record, std::move(diagnostic));
        return record;
    }
    return StartPlugin(*record, state_lock);
}

PluginRecord* Host::DiscoverPlugin(
    const std::filesystem::path& path,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    if (next_plugin_ == 0)
    {
        Write(KEEL_LOG_ERROR, "plugin handle space is exhausted");
        return nullptr;
    }
    auto plugin = std::make_unique<PluginRecord>();
    plugin->handle = next_plugin_++;
    plugin->path = path;
    PluginRecord* record = plugin.get();
    plugins_.push_back(std::move(plugin));

    std::string error;
    if (!record->library.Open(path, error))
    {
        record->diagnostic = "could not load module: " + error;
        Write(KEEL_LOG_ERROR, record->diagnostic + ": " + path.string());
        return record;
    }

    const auto query = reinterpret_cast<KeelPluginQueryFn>(record->library.Symbol("KeelPlugin_Query"));
    const auto manifest = reinterpret_cast<KeelPluginManifestFn>(
        record->library.Symbol("KeelPlugin_Manifest"));
    const auto load = reinterpret_cast<KeelPluginLoadFn>(record->library.Symbol("KeelPlugin_Load"));
    const auto unload = reinterpret_cast<KeelPluginUnloadFn>(record->library.Symbol("KeelPlugin_Unload"));
    if (!query || !load || !unload)
    {
        record->state = PluginState::invalid;
        record->diagnostic = "required plugin exports are missing";
        Write(KEEL_LOG_ERROR, "plugin query was rejected: " + path.string() + ": " + record->diagnostic);
        record->library.Close();
        return record;
    }

    const KeelHostQuery host_query{
        sizeof(KeelHostQuery),
        KEELS2_PLUGIN_ABI_VERSION,
        kHostVersion,
        game_.c_str(),
        platform_.c_str()
    };
    KeelPluginInfo info{};
    info.size = sizeof(info);
    KeelPluginManifest manifest_info{};
    manifest_info.size = sizeof(manifest_info);
    std::vector<PluginDependencyRecord> dependencies;
    bool query_succeeded{};
    bool manifest_succeeded{true};
    state_lock.unlock();
    try
    {
        query_succeeded = query(&host_query, &info) == KEEL_TRUE;
        if (query_succeeded && manifest)
        {
            manifest_succeeded = false;
            manifest_succeeded = manifest(&host_query, &manifest_info) == KEEL_TRUE;
            if (manifest_succeeded && manifest_info.size == sizeof(KeelPluginManifest) &&
                manifest_info.manifest_version == KEELS2_PLUGIN_MANIFEST_VERSION &&
                manifest_info.reserved == 0 && manifest_info.dependency_count <= 64 &&
                (manifest_info.dependency_count == 0 || manifest_info.dependencies))
            {
                dependencies.reserve(manifest_info.dependency_count);
                for (std::uint32_t index{}; index < manifest_info.dependency_count; ++index)
                {
                    const KeelPluginDependency& dependency = manifest_info.dependencies[index];
                    std::array<std::uint32_t, 3> parsed{};
                    if (dependency.size != sizeof(KeelPluginDependency) ||
                        (dependency.requirement != KEELS2_PLUGIN_DEPENDENCY_EXACT &&
                            dependency.requirement != KEELS2_PLUGIN_DEPENDENCY_AT_LEAST) ||
                        !ValidPluginName(dependency.name) ||
                        !ValidMetadataText(dependency.version, 63, false) ||
                        !ParseSemanticVersion(dependency.version, parsed))
                    {
                        manifest_succeeded = false;
                        break;
                    }
                    const bool duplicate = std::any_of(
                        dependencies.begin(), dependencies.end(), [&](const auto& existing) {
                            return EqualInsensitive(existing.name, dependency.name);
                        });
                    if (duplicate)
                    {
                        manifest_succeeded = false;
                        break;
                    }
                    dependencies.push_back({
                        dependency.name,
                        dependency.version,
                        dependency.requirement
                    });
                }
            }
            else
            {
                manifest_succeeded = false;
            }
        }
    }
    catch (...)
    {
    }
    state_lock.lock();
    if (!query_succeeded || !manifest_succeeded || info.size != sizeof(info) ||
        info.abi_version != KEELS2_PLUGIN_ABI_VERSION ||
        !ValidPluginName(info.name) ||
        !ValidMetadataText(info.version, 63, false) ||
        !ValidMetadataText(info.author, 127, true) ||
        !ValidMetadataText(info.description, 511, true))
    {
        record->state = PluginState::invalid;
        record->diagnostic = manifest_succeeded
            ? "query or metadata is incompatible"
            : "dependency manifest is incompatible";
        Write(
            KEEL_LOG_ERROR,
            "plugin query was rejected: " + path.string() + ": " + record->diagnostic
        );
        record->library.Close();
        return record;
    }

    record->name = info.name;
    record->author = info.author ? info.author : "";
    record->version = info.version;
    record->description = info.description ? info.description : "";
    record->dependencies = std::move(dependencies);
    if (std::any_of(record->dependencies.begin(), record->dependencies.end(), [&](const auto& dependency) {
            return EqualInsensitive(dependency.name, record->name);
        }))
    {
        record->state = PluginState::error;
        record->diagnostic = "plugin cannot depend on itself";
        record->selectable = true;
        Write(KEEL_LOG_ERROR, "plugin dependency was rejected: " + record->name + ": " +
            record->diagnostic);
        record->library.Close();
        return record;
    }
    const auto duplicate = std::find_if(plugins_.begin(), plugins_.end(), [record](const auto& candidate) {
        return candidate.get() != record && candidate->selectable &&
            EqualInsensitive(candidate->name, record->name);
    });
    if (duplicate != plugins_.end())
    {
        record->state = PluginState::error;
        record->diagnostic = "duplicate friendly name conflicts with " + (*duplicate)->path.filename().string();
        Write(
            KEEL_LOG_ERROR,
            "plugin friendly name conflict \"" + record->name + "\": " +
                (*duplicate)->path.filename().string() + " and " + path.filename().string()
        );
        record->library.Close();
        return record;
    }

    record->selectable = true;
    record->state = PluginState::loading;
    record->load = load;
    record->unload = unload;
    return record;
}

PluginRecord* Host::StartPlugin(
    PluginRecord& plugin,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    PluginRecord* record = &plugin;
    record->accepting_resources = true;
    record->loading = true;
    load_order_.push_back(record->handle);
    bool loaded{};
    bool load_threw{};
    state_lock.unlock();
    try
    {
        loaded = record->load && record->load(&api_, record->handle) == KEEL_TRUE;
    }
    catch (...)
    {
        load_threw = true;
    }
    state_lock.lock();
    if (load_threw)
    {
        record->diagnostic = "plugin threw during load";
    }
    if (loaded)
    {
        std::string dependency_diagnostic;
        if (!DependenciesReady(*record, dependency_diagnostic))
        {
            loaded = false;
            record->diagnostic = "dependency changed during load: " + dependency_diagnostic;
        }
    }
    record->loading = false;
    if (!loaded)
    {
        record->accepting_resources = false;
        SetCommandsOwnedEnabled(record->handle, false);
        KeelResult convar_release = KEEL_RESULT_OK;
        KeelResult lifecycle_release = KEEL_RESULT_OK;
        KeelResult plugin_service_release = KEEL_RESULT_OK;
        KeelResult source2_callbacks_release = KEEL_RESULT_OK;
        KeelResult release = KEEL_RESULT_OK;
        if (plugin_service_)
        {
            state_lock.unlock();
            plugin_service_release = plugin_service_->ReleasePlugin(record->handle);
            state_lock.lock();
        }
        if (convars_)
        {
            state_lock.unlock();
            convar_release = convars_->ReleasePlugin(record->handle);
            state_lock.lock();
        }
        if (lifecycle_)
        {
            state_lock.unlock();
            lifecycle_release = lifecycle_->ReleasePlugin(record->handle);
            state_lock.lock();
        }
        if (source2_callbacks_)
        {
            state_lock.unlock();
            source2_callbacks_release = source2_callbacks_->ReleasePlugin(record->handle);
            state_lock.lock();
        }
        if (keelhook_)
        {
            state_lock.unlock();
            release = keelhook_->ReleasePlugin(record->handle);
            state_lock.lock();
        }
        RemoveCommandsOwnedBy(record->handle);
        record->state = PluginState::error;
        if (plugin_service_release != KEEL_RESULT_OK ||
            source2_callbacks_release != KEEL_RESULT_OK || convar_release != KEEL_RESULT_OK ||
            lifecycle_release != KEEL_RESULT_OK || release != KEEL_RESULT_OK)
        {
            record->cleanup_pending = true;
            record->diagnostic = "native resource rollback failed; plugin module was retained";
            Write(KEEL_LOG_ERROR, "native resources could not be rolled back: " + record->name);
            return record;
        }
        state_lock.unlock();
        try
        {
            record->unload(record->handle);
        }
        catch (...)
        {
        }
        state_lock.lock();
        if (record->diagnostic.empty())
        {
            record->diagnostic = "load callback rejected startup";
        }
        Write(KEEL_LOG_ERROR, "plugin load was rejected: " + record->name);
        record->load = nullptr;
        record->unload = nullptr;
        record->library.Close();
        load_order_.erase(
            std::remove(load_order_.begin(), load_order_.end(), record->handle),
            load_order_.end());
        return record;
    }

    record->state = PluginState::loaded;
    SetCommandsOwnedEnabled(record->handle, true);
    if (keelhook_)
    {
        keelhook_->Activate(record->handle);
    }
    if (lifecycle_)
    {
        lifecycle_->Activate(record->handle);
    }
    if (convars_)
    {
        convars_->Activate(record->handle);
    }
    if (plugin_service_)
    {
        plugin_service_->Activate(record->handle);
    }
    if (source2_callbacks_)
    {
        source2_callbacks_->Activate(record->handle);
    }
    Write(KEEL_LOG_INFO, "plugin loaded: " + record->name + " " + record->version);
    PublishPluginEvent(KEELS2_PLUGIN_EVENT_LOADED, *record, state_lock);
    return record;
}

bool Host::DependenciesReady(const PluginRecord& plugin, std::string& diagnostic) const
{
    for (const auto& dependency : plugin.dependencies)
    {
        const auto target = std::find_if(plugins_.begin(), plugins_.end(), [&](const auto& candidate) {
            return candidate->selectable && EqualInsensitive(candidate->name, dependency.name);
        });
        if (target == plugins_.end())
        {
            diagnostic = "missing dependency " + dependency.name + " " + dependency.version;
            return false;
        }
        if (target->get() == &plugin)
        {
            diagnostic = "plugin cannot depend on itself";
            return false;
        }
        if (!DependencyVersionMatches((*target)->version, dependency))
        {
            diagnostic = "dependency version mismatch for " + dependency.name +
                ": found " + (*target)->version + ", required " + dependency.version;
            return false;
        }
        if ((*target)->state != PluginState::loaded || (*target)->transitioning)
        {
            diagnostic = "dependency is not running: " + dependency.name;
            return false;
        }
    }
    diagnostic.clear();
    return true;
}

bool Host::HasRunningDependent(const PluginRecord& plugin, std::string& dependent) const
{
    for (const auto& candidate : plugins_)
    {
        if (candidate.get() == &plugin || candidate->state != PluginState::loaded)
        {
            continue;
        }
        if (std::any_of(candidate->dependencies.begin(), candidate->dependencies.end(),
                [&](const auto& dependency) {
                    return EqualInsensitive(dependency.name, plugin.name);
                }))
        {
            dependent = candidate->name;
            return true;
        }
    }
    dependent.clear();
    return false;
}

void Host::RejectUnstartedPlugin(PluginRecord& plugin, std::string diagnostic)
{
    plugin.accepting_resources = false;
    plugin.loading = false;
    plugin.state = PluginState::error;
    plugin.diagnostic = std::move(diagnostic);
    plugin.load = nullptr;
    plugin.unload = nullptr;
    plugin.library.Close();
    Write(KEEL_LOG_ERROR, "plugin dependency was rejected: " + plugin.name + ": " +
        plugin.diagnostic);
}

bool Host::ResolvePluginPath(std::string_view filename, std::filesystem::path& path)
{
    if (filename.empty() || filename.size() > 255 || filename == "." || filename == ".." ||
        filename.find('/') != std::string_view::npos ||
        filename.find('\\') != std::string_view::npos ||
        filename.find(':') != std::string_view::npos ||
        std::any_of(filename.begin(), filename.end(), [](unsigned char character) {
            return std::iscntrl(character) != 0;
        }))
    {
        Write(KEEL_LOG_ERROR, "plugin filename must name one module in the plugins directory");
        return false;
    }

    const std::filesystem::path requested(filename);
    const std::string extension = requested.extension().string();
    if (!extension.empty() && !EqualInsensitive(extension, kPluginExtension))
    {
        Write(KEEL_LOG_ERROR, "plugin filename has an unsupported extension: " + std::string(filename));
        return false;
    }
    const std::string requested_name = requested.filename().string();
    const std::string requested_stem = extension.empty() ? requested_name : requested.stem().string();
    if (requested_stem.empty())
    {
        Write(KEEL_LOG_ERROR, "plugin filename is invalid");
        return false;
    }

    std::vector<std::filesystem::path> matches;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(plugin_directory_, error), end;
         !error && iterator != end;
         iterator.increment(error))
    {
        const auto status = iterator->symlink_status(error);
        if (error)
        {
            break;
        }
        if (!std::filesystem::is_regular_file(status) ||
            !EqualInsensitive(iterator->path().extension().string(), kPluginExtension))
        {
            continue;
        }
        const std::string candidate_name = iterator->path().filename().string();
        const std::string candidate_stem = iterator->path().stem().string();
        const bool match = extension.empty()
            ? EqualInsensitive(candidate_stem, requested_stem)
            : EqualInsensitive(candidate_name, requested_name);
        if (match)
        {
            matches.push_back(iterator->path());
        }
    }
    if (error)
    {
        Write(KEEL_LOG_ERROR, "could not enumerate plugins: " + error.message());
        return false;
    }
    if (matches.empty())
    {
        Write(KEEL_LOG_ERROR, "plugin file \"" + std::string(filename) + "\" was not found");
        return false;
    }
    if (matches.size() != 1)
    {
        Write(KEEL_LOG_ERROR, "plugin filename \"" + std::string(filename) + "\" is ambiguous");
        return false;
    }
    path = matches.front();
    return true;
}

void Host::LoadPluginCommand(
    std::string_view filename,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    std::filesystem::path path;
    if (!ResolvePluginPath(filename, path))
    {
        return;
    }

    const auto existing = std::find_if(plugins_.begin(), plugins_.end(), [&path](const auto& plugin) {
        return EqualInsensitive(plugin->path.filename().string(), path.filename().string());
    });
    if (existing != plugins_.end())
    {
        if ((*existing)->state == PluginState::loaded ||
            (*existing)->state == PluginState::paused)
        {
            Write(
                KEEL_LOG_ERROR,
                "plugin file \"" + path.filename().string() + "\" is already loaded as [" +
                    PluginDisplayId(existing->get()) + "] " + (*existing)->name
            );
            return;
        }
        if ((*existing)->cleanup_pending || (*existing)->library.IsOpen())
        {
            Write(
                KEEL_LOG_ERROR,
                "plugin file \"" + path.filename().string() +
                    "\" is retained until native cleanup completes"
            );
            return;
        }
        RemovePluginRecord((*existing)->handle);
    }

    LoadPlugin(path, state_lock);
}

void Host::UnloadPluginCommand(
    std::string_view selector,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    PluginRecord* plugin = SelectPlugin(selector);
    if (!plugin)
    {
        return;
    }
    if (plugin->state != PluginState::loaded && plugin->state != PluginState::paused)
    {
        Write(KEEL_LOG_ERROR, "plugin [" + PluginDisplayId(plugin) + "] is not loaded");
        return;
    }
    PluginTransition transition(*plugin);
    if (!transition)
    {
        Write(KEEL_LOG_ERROR, "plugin transition is already active: " + plugin->name);
        return;
    }
    std::string dependent;
    if (HasRunningDependent(*plugin, dependent))
    {
        Write(
            KEEL_LOG_ERROR,
            "plugin unload is blocked by running dependent " + dependent + ": " + plugin->name
        );
        return;
    }

    const KeelPluginHandle handle = plugin->handle;
    const std::string display_id = PluginDisplayId(plugin);
    const std::string name = plugin->name;
    const bool was_running = plugin->state == PluginState::loaded;
    plugin->accepting_resources = false;
    SetCommandsOwnedEnabled(handle, false);
    if (was_running && source2_callbacks_)
    {
        state_lock.unlock();
        const KeelResult quiescence = source2_callbacks_->Deactivate(handle);
        state_lock.lock();
        if (quiescence != KEEL_RESULT_OK)
        {
            plugin->accepting_resources = true;
            SetCommandsOwnedEnabled(handle, true);
            source2_callbacks_->Activate(handle);
            Write(KEEL_LOG_ERROR, "plugin unload is busy in a Source 2 callback: " + name);
            return;
        }
    }
    if (was_running && lifecycle_)
    {
        state_lock.unlock();
        const KeelResult quiescence = lifecycle_->Deactivate(handle);
        state_lock.lock();
        if (quiescence != KEEL_RESULT_OK)
        {
            RestorePluginDispatch(*plugin);
            Write(KEEL_LOG_ERROR, "plugin unload is busy in a lifecycle callback: " + name);
            return;
        }
    }
    if (was_running && convars_)
    {
        state_lock.unlock();
        const KeelResult quiescence = convars_->Deactivate(handle);
        state_lock.lock();
        if (quiescence != KEEL_RESULT_OK)
        {
            RestorePluginDispatch(*plugin);
            Write(KEEL_LOG_ERROR, "plugin unload is busy in a ConVar callback: " + name);
            return;
        }
    }
    if (was_running && plugin_service_)
    {
        state_lock.unlock();
        const KeelResult quiescence = plugin_service_->Deactivate(handle);
        state_lock.lock();
        if (quiescence != KEEL_RESULT_OK)
        {
            RestorePluginDispatch(*plugin);
            Write(KEEL_LOG_ERROR, "plugin unload is busy in a plugin event callback: " + name);
            return;
        }
    }
    if (was_running && keelhook_)
    {
        state_lock.unlock();
        const KeelResult quiescence = keelhook_->Deactivate(handle);
        state_lock.lock();
        if (quiescence != KEEL_RESULT_OK)
        {
            RestorePluginDispatch(*plugin);
            Write(KEEL_LOG_ERROR, "plugin unload is busy in KeelHook: " + name);
            return;
        }
    }
    if (keelhook_)
    {
        state_lock.unlock();
        const KeelResult release = keelhook_->ReleasePlugin(handle);
        state_lock.lock();
        if (release != KEEL_RESULT_OK)
        {
            if (release == KEEL_RESULT_BUSY && !plugin->cleanup_pending && was_running)
            {
                RestorePluginDispatch(*plugin);
            }
            else
            {
                plugin->cleanup_pending = true;
                plugin->diagnostic = "KeelHook cleanup is incomplete; plugin is quarantined";
            }
            Write(
                KEEL_LOG_ERROR,
                release == KEEL_RESULT_BUSY
                    ? "plugin unload is busy in KeelHook: " + name
                    : "plugin unload could not restore KeelHook resources: " + name
            );
            return;
        }
    }
    if (lifecycle_)
    {
        state_lock.unlock();
        const KeelResult release = lifecycle_->ReleasePlugin(handle);
        state_lock.lock();
        if (release != KEEL_RESULT_OK)
        {
            plugin->cleanup_pending = true;
            plugin->diagnostic = "lifecycle cleanup is incomplete; plugin is quarantined";
            Write(KEEL_LOG_ERROR, "plugin unload could not release lifecycle resources: " + name);
            return;
        }
    }
    if (source2_callbacks_)
    {
        state_lock.unlock();
        const KeelResult release = source2_callbacks_->ReleasePlugin(handle);
        state_lock.lock();
        if (release != KEEL_RESULT_OK)
        {
            plugin->cleanup_pending = true;
            plugin->diagnostic = "Source 2 callback cleanup is incomplete; plugin is quarantined";
            Write(KEEL_LOG_ERROR, "plugin unload could not release Source 2 callbacks: " + name);
            return;
        }
    }
    if (convars_)
    {
        state_lock.unlock();
        const KeelResult release = convars_->ReleasePlugin(handle);
        state_lock.lock();
        if (release != KEEL_RESULT_OK)
        {
            plugin->cleanup_pending = true;
            plugin->diagnostic = "ConVar cleanup is incomplete; plugin is quarantined";
            Write(KEEL_LOG_ERROR, "plugin unload could not release ConVar resources: " + name);
            return;
        }
    }
    if (plugin_service_)
    {
        state_lock.unlock();
        const KeelResult release = plugin_service_->ReleasePlugin(handle);
        state_lock.lock();
        if (release != KEEL_RESULT_OK)
        {
            plugin->cleanup_pending = true;
            plugin->diagnostic = "plugin event cleanup is incomplete; plugin is quarantined";
            Write(KEEL_LOG_ERROR, "plugin unload could not release plugin event resources: " + name);
            return;
        }
    }
    RemoveCommandsOwnedBy(handle);
    if (plugin->unload)
    {
        state_lock.unlock();
        try
        {
            plugin->unload(handle);
        }
        catch (...)
        {
            Write(KEEL_LOG_ERROR, "plugin threw during unload: " + name);
        }
        state_lock.lock();
    }
    plugin->load = nullptr;
    plugin->unload = nullptr;
    plugin->library.Close();
    KeelPluginSnapshot snapshot{};
    FillPluginSnapshot(*plugin, snapshot);
    snapshot.state = KEELS2_PLUGIN_STATE_UNKNOWN;
    transition.Disarm();
    RemovePluginRecord(handle);
    if (plugin_service_)
    {
        state_lock.unlock();
        plugin_service_->Publish(KEELS2_PLUGIN_EVENT_UNLOADED, snapshot);
        state_lock.lock();
    }
    Write(KEEL_LOG_INFO, "plugin unloaded: [" + display_id + "] " + name);
}

void Host::PausePluginCommand(
    std::string_view selector,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    PluginRecord* plugin = SelectPlugin(selector);
    if (plugin)
    {
        static_cast<void>(PausePlugin(plugin->handle, state_lock, true));
    }
}

void Host::ResumePluginCommand(
    std::string_view selector,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    PluginRecord* plugin = SelectPlugin(selector);
    if (plugin)
    {
        static_cast<void>(ResumePlugin(plugin->handle, state_lock, true));
    }
}

KeelResult Host::PausePlugin(
    KeelPluginHandle target,
    std::unique_lock<std::recursive_mutex>& state_lock,
    bool report)
{
    PluginRecord* plugin = PluginByHandle(target);
    if (!plugin)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (plugin->state == PluginState::paused)
    {
        return KEEL_RESULT_ALREADY_EXISTS;
    }
    if (plugin->state != PluginState::loaded)
    {
        if (report)
        {
            Write(KEEL_LOG_ERROR, "plugin [" + PluginDisplayId(plugin) + "] is not running");
        }
        return KEEL_RESULT_NOT_READY;
    }
    PluginTransition transition(*plugin);
    if (!transition)
    {
        if (report)
        {
            Write(KEEL_LOG_ERROR, "plugin transition is already active: " + plugin->name);
        }
        return KEEL_RESULT_BUSY;
    }
    std::string dependent;
    if (HasRunningDependent(*plugin, dependent))
    {
        if (report)
        {
            Write(
                KEEL_LOG_ERROR,
                "plugin pause is blocked by running dependent " + dependent + ": " + plugin->name
            );
        }
        return KEEL_RESULT_BUSY;
    }

    plugin->accepting_resources = false;
    SetCommandsOwnedEnabled(target, false);
    const auto fail = [&](KeelResult result, const char* layer) {
        RestorePluginDispatch(*plugin);
        if (report)
        {
            Write(KEEL_LOG_ERROR, "plugin pause is busy in " + std::string(layer) + ": " + plugin->name);
        }
        return result;
    };
    if (source2_callbacks_)
    {
        state_lock.unlock();
        const KeelResult result = source2_callbacks_->Deactivate(target);
        state_lock.lock();
        if (result != KEEL_RESULT_OK)
        {
            return fail(result, "a Source 2 callback");
        }
    }
    if (lifecycle_)
    {
        state_lock.unlock();
        const KeelResult result = lifecycle_->Deactivate(target);
        state_lock.lock();
        if (result != KEEL_RESULT_OK)
        {
            return fail(result, "a lifecycle callback");
        }
    }
    if (convars_)
    {
        state_lock.unlock();
        const KeelResult result = convars_->Deactivate(target);
        state_lock.lock();
        if (result != KEEL_RESULT_OK)
        {
            return fail(result, "a ConVar callback");
        }
    }
    if (plugin_service_)
    {
        state_lock.unlock();
        const KeelResult result = plugin_service_->Deactivate(target);
        state_lock.lock();
        if (result != KEEL_RESULT_OK)
        {
            return fail(result, "a plugin event callback");
        }
    }
    if (keelhook_)
    {
        state_lock.unlock();
        const KeelResult result = keelhook_->Deactivate(target);
        state_lock.lock();
        if (result != KEEL_RESULT_OK)
        {
            return fail(result, "KeelHook");
        }
    }

    plugin->state = PluginState::paused;
    Write(KEEL_LOG_INFO, "plugin paused: [" + PluginDisplayId(plugin) + "] " + plugin->name);
    PublishPluginEvent(KEELS2_PLUGIN_EVENT_PAUSED, *plugin, state_lock);
    return KEEL_RESULT_OK;
}

KeelResult Host::ResumePlugin(
    KeelPluginHandle target,
    std::unique_lock<std::recursive_mutex>& state_lock,
    bool report)
{
    PluginRecord* plugin = PluginByHandle(target);
    if (!plugin)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (plugin->state == PluginState::loaded)
    {
        return KEEL_RESULT_ALREADY_EXISTS;
    }
    if (plugin->state != PluginState::paused)
    {
        if (report)
        {
            Write(KEEL_LOG_ERROR, "plugin [" + PluginDisplayId(plugin) + "] is not paused");
        }
        return KEEL_RESULT_NOT_READY;
    }
    PluginTransition transition(*plugin);
    if (!transition)
    {
        if (report)
        {
            Write(KEEL_LOG_ERROR, "plugin transition is already active: " + plugin->name);
        }
        return KEEL_RESULT_BUSY;
    }
    std::string diagnostic;
    if (!DependenciesReady(*plugin, diagnostic))
    {
        if (report)
        {
            Write(KEEL_LOG_ERROR, "plugin resume was rejected: " + plugin->name + ": " + diagnostic);
        }
        return KEEL_RESULT_NOT_READY;
    }
    plugin->state = PluginState::loaded;
    RestorePluginDispatch(*plugin);
    Write(KEEL_LOG_INFO, "plugin resumed: [" + PluginDisplayId(plugin) + "] " + plugin->name);
    PublishPluginEvent(KEELS2_PLUGIN_EVENT_RESUMED, *plugin, state_lock);
    return KEEL_RESULT_OK;
}

void Host::RestorePluginDispatch(PluginRecord& plugin)
{
    plugin.accepting_resources = true;
    SetCommandsOwnedEnabled(plugin.handle, true);
    if (keelhook_)
    {
        keelhook_->Activate(plugin.handle);
    }
    if (lifecycle_)
    {
        lifecycle_->Activate(plugin.handle);
    }
    if (convars_)
    {
        convars_->Activate(plugin.handle);
    }
    if (plugin_service_)
    {
        plugin_service_->Activate(plugin.handle);
    }
    if (source2_callbacks_)
    {
        source2_callbacks_->Activate(plugin.handle);
    }
}

void Host::PublishPluginEvent(
    KeelPluginEventType event,
    const PluginRecord& plugin,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    if (!plugin_service_)
    {
        return;
    }
    KeelPluginSnapshot snapshot{};
    FillPluginSnapshot(plugin, snapshot);
    state_lock.unlock();
    plugin_service_->Publish(event, snapshot);
    state_lock.lock();
}

void Host::FillPluginSnapshot(
    const PluginRecord& plugin,
    KeelPluginSnapshot& snapshot) const noexcept
{
    snapshot = {};
    snapshot.size = sizeof(snapshot);
    snapshot.state = PublicPluginState(plugin.state);
    snapshot.handle = plugin.handle;
    CopySnapshotText(snapshot.name, plugin.name);
    CopySnapshotText(snapshot.author, plugin.author);
    CopySnapshotText(snapshot.version, plugin.version);
    CopySnapshotText(snapshot.description, plugin.description);
    CopySnapshotText(snapshot.file, plugin.path.filename().string());
    CopySnapshotText(snapshot.diagnostic, plugin.diagnostic);
}

void Host::RemovePluginRecord(KeelPluginHandle handle)
{
    load_order_.erase(std::remove(load_order_.begin(), load_order_.end(), handle), load_order_.end());
    plugins_.erase(
        std::remove_if(plugins_.begin(), plugins_.end(), [handle](const auto& plugin) {
            return plugin->handle == handle;
        }),
        plugins_.end()
    );
}

void Host::RemoveCommandsOwnedBy(KeelPluginHandle owner)
{
    for (auto iterator = commands_.begin(); iterator != commands_.end();)
    {
        if (iterator->second->owner != owner)
        {
            ++iterator;
            continue;
        }
        std::unique_ptr<CommandRecord> command = std::move(iterator->second);
        command->enabled.store(false, std::memory_order_release);
        if (adapter_)
        {
            adapter_->UnregisterCommand(command->game_handle);
        }
        command->callback = nullptr;
        command->native_callback = nullptr;
        command->user_data = nullptr;
        iterator = commands_.erase(iterator);
        retired_commands_.push_back(std::move(command));
    }
}

void Host::SetCommandsOwnedEnabled(KeelPluginHandle owner, bool enabled)
{
    for (auto& [handle, command] : commands_)
    {
        static_cast<void>(handle);
        if (command->owner == owner)
        {
            command->enabled.store(enabled, std::memory_order_release);
        }
    }
}

PluginRecord* Host::PluginByHandle(KeelPluginHandle handle)
{
    const auto iterator = std::find_if(plugins_.begin(), plugins_.end(), [handle](const auto& plugin) {
        return plugin->handle == handle;
    });
    return iterator == plugins_.end() ? nullptr : iterator->get();
}

const PluginRecord* Host::PluginByHandle(KeelPluginHandle handle) const
{
    const auto iterator = std::find_if(plugins_.begin(), plugins_.end(), [handle](const auto& plugin) {
        return plugin->handle == handle;
    });
    return iterator == plugins_.end() ? nullptr : iterator->get();
}

PluginRecord* Host::SelectPlugin(std::string_view selector)
{
    if (selector.empty())
    {
        Write(KEEL_LOG_ERROR, "plugin selector is required");
        return nullptr;
    }

    const bool numeric = std::all_of(selector.begin(), selector.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
    if (numeric)
    {
        std::size_t index{};
        const auto result = std::from_chars(selector.data(), selector.data() + selector.size(), index);
        PluginRecord* plugin = result.ec == std::errc{} && result.ptr == selector.data() + selector.size() &&
                index != 0 && index <= plugins_.size()
            ? plugins_[index - 1].get()
            : nullptr;
        if (!plugin)
        {
            Write(KEEL_LOG_ERROR, "plugin \"" + std::string(selector) + "\" was not found");
        }
        return plugin;
    }

    std::vector<PluginRecord*> matches;
    for (const auto& plugin : plugins_)
    {
        if (plugin->selectable && EqualInsensitive(plugin->name, selector))
        {
            return plugin.get();
        }
    }
    for (const auto& plugin : plugins_)
    {
        if (plugin->selectable && StartsWithInsensitive(plugin->name, selector))
        {
            matches.push_back(plugin.get());
        }
    }
    if (matches.empty())
    {
        for (const auto& plugin : plugins_)
        {
            if (plugin->selectable && ContainsInsensitive(plugin->name, selector))
            {
                matches.push_back(plugin.get());
            }
        }
    }
    if (matches.size() == 1)
    {
        return matches.front();
    }
    if (matches.empty())
    {
        Write(KEEL_LOG_ERROR, "plugin \"" + std::string(selector) + "\" was not found");
        return nullptr;
    }

    Write(KEEL_LOG_ERROR, "plugin selector \"" + std::string(selector) + "\" is ambiguous:");
    for (const PluginRecord* plugin : matches)
    {
        WriteLine("  [" + PluginDisplayId(plugin) + "] " + plugin->name);
    }
    return nullptr;
}

std::size_t Host::PluginDisplayIndex(const PluginRecord* plugin) const
{
    const auto iterator = std::find_if(plugins_.begin(), plugins_.end(), [plugin](const auto& candidate) {
        return candidate.get() == plugin;
    });
    return iterator == plugins_.end()
        ? 0
        : static_cast<std::size_t>(std::distance(plugins_.begin(), iterator)) + 1;
}

std::string Host::PluginDisplayId(const PluginRecord* plugin) const
{
    return FormatPluginIndex(PluginDisplayIndex(plugin));
}

std::size_t Host::PluginCommandCount(KeelPluginHandle owner) const
{
    return static_cast<std::size_t>(std::count_if(commands_.begin(), commands_.end(), [owner](const auto& entry) {
        return entry.second->owner == owner;
    }));
}

bool Host::ValidMetadataText(const char* text, std::size_t maximum, bool allow_empty)
{
    if (!text)
    {
        return allow_empty;
    }
    const std::string_view value(text);
    if (value.empty())
    {
        return allow_empty;
    }
    if (value.size() > maximum || std::isspace(static_cast<unsigned char>(value.front())) ||
        std::isspace(static_cast<unsigned char>(value.back())))
    {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](unsigned char character) {
        return std::iscntrl(character) != 0;
    });
}

bool Host::ValidPluginName(const char* name)
{
    if (!ValidMetadataText(name, 127, false))
    {
        return false;
    }
    const std::string_view value(name);
    return !std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

bool Host::EqualInsensitive(std::string_view left, std::string_view right)
{
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(), [](unsigned char first, unsigned char second) {
            return std::tolower(first) == std::tolower(second);
        });
}

bool Host::StartsWithInsensitive(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && EqualInsensitive(text.substr(0, prefix.size()), prefix);
}

bool Host::ContainsInsensitive(std::string_view text, std::string_view part)
{
    if (part.empty())
    {
        return true;
    }
    for (std::size_t position{}; position + part.size() <= text.size(); ++position)
    {
        if (EqualInsensitive(text.substr(position, part.size()), part))
        {
            return true;
        }
    }
    return false;
}

std::string Host::FormatPluginIndex(std::size_t index)
{
    std::string result = std::to_string(index);
    if (result.size() < 2)
    {
        result.insert(result.begin(), '0');
    }
    return result;
}

const char* Host::PluginStateLabel(PluginState state)
{
    if (state == PluginState::loading)
    {
        return "loading";
    }
    if (state == PluginState::loaded)
    {
        return "loaded";
    }
    if (state == PluginState::paused)
    {
        return "paused";
    }
    if (state == PluginState::invalid)
    {
        return "invalid";
    }
    return "error";
}

KeelPluginRuntimeState Host::PublicPluginState(PluginState state) noexcept
{
    switch (state)
    {
        case PluginState::loading:
            return KEELS2_PLUGIN_STATE_LOADING;
        case PluginState::loaded:
            return KEELS2_PLUGIN_STATE_RUNNING;
        case PluginState::paused:
            return KEELS2_PLUGIN_STATE_PAUSED;
        case PluginState::invalid:
            return KEELS2_PLUGIN_STATE_INVALID;
        case PluginState::error:
            return KEELS2_PLUGIN_STATE_ERROR;
    }
    return KEELS2_PLUGIN_STATE_UNKNOWN;
}

bool Host::ParseSemanticVersion(
    std::string_view version,
    std::array<std::uint32_t, 3>& output) noexcept
{
    output = {};
    std::size_t begin{};
    for (std::size_t component{}; component < output.size(); ++component)
    {
        const std::size_t end = component + 1 == output.size()
            ? version.size()
            : version.find('.', begin);
        if (end == std::string_view::npos || end == begin)
        {
            return false;
        }
        const char* first = version.data() + begin;
        const char* last = version.data() + end;
        const auto result = std::from_chars(first, last, output[component]);
        if (result.ec != std::errc{} || result.ptr != last)
        {
            return false;
        }
        if (component + 1 != output.size())
        {
            begin = end + 1;
        }
    }
    return begin <= version.size() && version.find('.', begin) == std::string_view::npos;
}

bool Host::DependencyVersionMatches(
    std::string_view actual,
    const PluginDependencyRecord& dependency) noexcept
{
    std::array<std::uint32_t, 3> actual_version{};
    std::array<std::uint32_t, 3> required_version{};
    if (!ParseSemanticVersion(actual, actual_version) ||
        !ParseSemanticVersion(dependency.version, required_version))
    {
        return false;
    }
    if (dependency.requirement == KEELS2_PLUGIN_DEPENDENCY_EXACT)
    {
        return actual_version == required_version;
    }
    if (dependency.requirement == KEELS2_PLUGIN_DEPENDENCY_AT_LEAST)
    {
        return actual_version >= required_version;
    }
    return false;
}

}
