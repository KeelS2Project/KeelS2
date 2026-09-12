#include <keels2/authoring.hpp>

#include <cstring>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{

using namespace keels2::authoring;

std::uint32_t g_mode{};
std::uint32_t g_load_count{};
std::uint32_t g_unload_count{};
std::uint32_t g_loaded_count{};
std::uint32_t g_unloaded_count{};
std::uint32_t g_paused_count{};
std::uint32_t g_resumed_count{};
std::uint32_t g_all_loaded_count{};
bool g_event_values_valid{true};
bool g_helpers_valid{};
bool g_unload_helpers_disabled{};

class PluginAuthoringRuntime final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Plugin Authoring Runtime",
        "KeelS2 Tests",
        "0.5.4",
        "Validates the friendly managed-plugin facade"
    };

    std::vector<PluginDependency> Dependencies() const override
    {
        if (g_mode == 1)
        {
            throw std::runtime_error("dependency manifest exception");
        }
        return {
            {"Core Plugin", "1.2.3", DependencyRequirement::exact},
            {"Utility Plugin", "2.0.0", DependencyRequirement::at_least}
        };
    }

    bool Load() override
    {
        ++g_load_count;
        const auto plugins = Plugins();
        const auto target = FindPlugin("Target Plugin");
        g_helpers_valid = plugins.size() == 2 && target && target->id == 22 &&
            target->status == PluginStatus::running && target->name == "Target Plugin" &&
            target->author == "KeelS2 Tests" && target->version == "3.4.5" &&
            target->description == "Runtime target" && target->file == "target.so" &&
            target->diagnostic.empty();
        PluginDetails details;
        g_helpers_valid = g_helpers_valid && GetPlugin("Target Plugin", details) &&
            details.handle == 22 && std::strcmp(details.name, "Target Plugin") == 0 &&
            GetPlugin(PluginId{22}, details) && details.state == KEELS2_PLUGIN_STATE_RUNNING;
        uint32 count{};
        while (GetPluginAt(count, details))
        {
            ++count;
        }
        g_helpers_valid = g_helpers_valid && count == 2 &&
            LastResult() == KEEL_RESULT_NOT_FOUND && details.handle == 0 && details.name[0] == 0 &&
            !GetPlugin("Missing Plugin", details) && LastResult() == KEEL_RESULT_NOT_FOUND &&
            !GetPlugin(static_cast<const char*>(nullptr), details) && LastResult() == KEEL_RESULT_INVALID_ARGUMENT &&
            !GetPlugin(PluginId{}, details) && LastResult() == KEEL_RESULT_INVALID_ARGUMENT;
        return g_helpers_valid;
    }

    void Unload() override
    {
        ++g_unload_count;
        PluginDetails details;
        g_helpers_valid = g_helpers_valid && !GetPlugin("Target Plugin", details) &&
            LastResult() == KEEL_RESULT_NOT_READY && details.handle == 0;
        g_unload_helpers_disabled = Plugins().empty() && !FindPlugin("Target Plugin") &&
            !PausePlugin(22) && !ResumePlugin(22);
    }

    void OnPluginLoaded(const PluginSnapshot& plugin) override
    {
        ++g_loaded_count;
        Validate(plugin, PluginStatus::running);
        g_helpers_valid = g_helpers_valid && PausePlugin(plugin.id) && ResumePlugin(plugin.id);
    }

    void OnPluginUnloaded(const PluginSnapshot& plugin) override
    {
        ++g_unloaded_count;
        Validate(plugin, PluginStatus::unknown);
    }

    void OnPluginPaused(const PluginSnapshot& plugin) override
    {
        ++g_paused_count;
        Validate(plugin, PluginStatus::paused);
        throw std::runtime_error("plugin event exception");
    }

    void OnPluginResumed(const PluginSnapshot& plugin) override
    {
        ++g_resumed_count;
        Validate(plugin, PluginStatus::running);
    }

    void OnAllPluginsLoaded() override
    {
        ++g_all_loaded_count;
    }

private:
    static void Validate(const PluginSnapshot& plugin, PluginStatus status)
    {
        g_event_values_valid = g_event_values_valid && plugin.id == 22 &&
            plugin.status == status && plugin.name == "Target Plugin" &&
            plugin.author == "KeelS2 Tests" && plugin.version == "3.4.5" &&
            plugin.description == "Runtime target" && plugin.file == "target.so" &&
            plugin.diagnostic.empty();
    }
};

class StaticRequirements final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        .name = "Static Requirements",
        .author = "KeelS2 Tests",
        .version = "1.0.0",
        .description = "Validates compile-time dependency declarations"
    };
    static constexpr PluginRequirement Requirements[]{
        {.name = "Core Plugin", .version = "1.2.3", .requirement = DependencyRequirement::exact},
        {.name = "Utility Plugin", .version = "2.0.0"}
    };
};

}

KEELS2_PLUGIN(PluginAuthoringRuntime)

extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelTest_StaticRequirements(
    const KeelHostQuery* query, KeelPluginManifest* output)
{
    return keels2::detail::AuthoringAdapter<StaticRequirements>::Manifest(query, output);
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_PluginAuthoringRuntimeReset()
{
    g_mode = 0;
    g_load_count = 0;
    g_unload_count = 0;
    g_loaded_count = 0;
    g_unloaded_count = 0;
    g_paused_count = 0;
    g_resumed_count = 0;
    g_all_loaded_count = 0;
    g_event_values_valid = true;
    g_helpers_valid = false;
    g_unload_helpers_disabled = false;
}

extern "C" KEELS2_PLUGIN_EXPORT void KeelTest_PluginAuthoringRuntimeMode(std::uint32_t mode)
{
    g_mode = mode;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_PluginAuthoringRuntimeValue(
    std::uint32_t value)
{
    switch (value)
    {
        case 0:
            return g_load_count;
        case 1:
            return g_unload_count;
        case 2:
            return g_loaded_count;
        case 3:
            return g_unloaded_count;
        case 4:
            return g_paused_count;
        case 5:
            return g_resumed_count;
        case 6:
            return g_all_loaded_count;
        case 7:
            return g_event_values_valid ? 1u : 0u;
        case 8:
            return g_helpers_valid ? 1u : 0u;
        case 9:
            return g_unload_helpers_disabled ? 1u : 0u;
        default:
            return 0;
    }
}
