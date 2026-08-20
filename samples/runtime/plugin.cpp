#include "plugin.h"

#include <string>

namespace
{

const char* StatusName(PluginStatus status)
{
    switch (status)
    {
        case PluginStatus::loading:
            return "loading";
        case PluginStatus::running:
            return "running";
        case PluginStatus::paused:
            return "paused";
        case PluginStatus::invalid:
            return "invalid";
        case PluginStatus::error:
            return "error";
        default:
            return "unknown";
    }
}

}

std::vector<PluginDependency> RuntimePlugin::Dependencies() const
{
    return {{
        "KeelS2 ConVar Sample",
        "0.5.0",
        DependencyRequirement::at_least
    }};
}

bool RuntimePlugin::Load()
{
    const auto dependency = FindPlugin("KeelS2 ConVar Sample");
    if (!dependency || dependency->status != PluginStatus::running)
    {
        LogError("required ConVar sample is not running");
        return false;
    }
    LogMessage("dependency snapshot is running");
    return CreateCommand(
        "keel_runtime_sample",
        "Lists plugins or inspects one plugin by friendly name",
        &RuntimePlugin::Inspect);
}

void RuntimePlugin::OnPluginLoaded(const PluginSnapshot& plugin)
{
    LogSnapshot("loaded", plugin);
}

void RuntimePlugin::OnPluginUnloaded(const PluginSnapshot& plugin)
{
    LogSnapshot("unloaded", plugin);
}

void RuntimePlugin::OnPluginPaused(const PluginSnapshot& plugin)
{
    LogSnapshot("paused", plugin);
}

void RuntimePlugin::OnPluginResumed(const PluginSnapshot& plugin)
{
    LogSnapshot("resumed", plugin);
}

void RuntimePlugin::OnAllPluginsLoaded()
{
    const std::string message =
        "all initial plugins loaded; snapshot count=" + std::to_string(Plugins().size());
    LogMessage(message.c_str());
}

void RuntimePlugin::Inspect(const CCommandContext& context, const CCommand& command)
{
    static_cast<void>(context);
    if (command.ArgC() == 1)
    {
        const std::string message = "snapshot count=" + std::to_string(Plugins().size());
        LogMessage(message.c_str());
        return;
    }
    if (command.ArgC() != 2)
    {
        LogError("usage: keel_runtime_sample [plugin friendly name]");
        return;
    }
    const auto plugin = FindPlugin(command[1]);
    if (!plugin)
    {
        LogWarning("plugin was not found");
        return;
    }
    LogSnapshot("snapshot", *plugin);
}

void RuntimePlugin::LogSnapshot(const char* event, const PluginSnapshot& plugin)
{
    const std::string message = std::string(event) + " [" + std::to_string(plugin.id) +
        "] " + plugin.name + " " + plugin.version + " - " + StatusName(plugin.status);
    LogMessage(message.c_str());
}

KEELS2_PLUGIN(RuntimePlugin)
