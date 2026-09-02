#ifndef KEELS2_SAMPLE_RUNTIME_PLUGIN_H
#define KEELS2_SAMPLE_RUNTIME_PLUGIN_H

#include <keels2/keels2.hpp>

class RuntimePlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 Runtime Sample",
        "KeelS2 Project",
        "0.9.0",
        "Plugin snapshots, dependencies, and managed runtime events"
    };

    std::vector<PluginDependency> Dependencies() const override;
    bool Load() override;

    void OnPluginLoaded(const PluginSnapshot& plugin) override;
    void OnPluginUnloaded(const PluginSnapshot& plugin) override;
    void OnPluginPaused(const PluginSnapshot& plugin) override;
    void OnPluginResumed(const PluginSnapshot& plugin) override;
    void OnAllPluginsLoaded() override;

private:
    void Inspect(const CCommandContext& context, const CCommand& command);
    void LogSnapshot(const char* event, const PluginSnapshot& plugin);
};

#endif
