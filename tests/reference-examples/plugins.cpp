#include <keels2/authoring.hpp>

namespace docs
{
using namespace keels2::authoring;
class Plugins final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Plugins", "KeelS2 documentation", "1.0.0", "Read plugin metadata and dependencies"};
    static constexpr PluginRequirement Requirements[]{
        {"Docs First Plugin", "1.0.0", DependencyRequirement::at_least}
    };
    bool Load() override { return CreateCommand("keel_docs_plugins", "List loaded plugin details", &Plugins::List); }
    void OnPluginLoaded(const PluginSnapshot& plugin) override { LogMessage("Plugin loaded: {}", plugin.name); }
    void OnPluginPaused(const PluginSnapshot& plugin) override { LogMessage("Plugin paused: {}", plugin.name); }
    void OnPluginResumed(const PluginSnapshot& plugin) override { LogMessage("Plugin resumed: {}", plugin.name); }
    void OnPluginUnloaded(const PluginSnapshot& plugin) override { LogMessage("Plugin unloaded: {}", plugin.name); }
private:
    void List(const CCommandContext&, const CCommand&)
    {
        PluginDetails plugin{};
        for (uint32 index = 0; GetPluginAt(index, plugin); ++index)
            LogMessage("{} {}", plugin.name, plugin.version);
        if (LastResult() != KEEL_RESULT_NOT_FOUND) LogError("Plugin list: {}", LastError());
        if (GetPlugin("Docs First Plugin", plugin))
        {
            PluginDetails byId{};
            if (GetPlugin(plugin.handle, byId)) LogMessage("Dependency resolved: {}", byId.name);
        }
    }
};
}
KEELS2_PLUGIN(docs::Plugins)
