#include <keels2/authoring.hpp>

namespace docs
{
using namespace keels2::authoring;
class EntityCreation final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Entity Creation", "KeelS2 documentation", "1.0.0", "Create a prop with owned pending state"};
    bool Load() override
    {
        return CreateCommand("keel_docs_spawn", "Create a prop: <model> \"x y z\"", &EntityCreation::Spawn);
    }
private:
    void Spawn(const CCommandContext&, const CCommand& arguments)
    {
        if (arguments.ArgC() != 3)
        {
            LogWarning("Usage: keel_docs_spawn <model asset> \"x y z\"");
            return;
        }
        Entity prop;
        if (!EntityConstructionAvailable() || !CreateEntity("prop_dynamic", prop))
        {
            LogWarning("Create failed: {}", LastError());
            return;
        }
        if (!prop.SetKey("model", arguments[1]) || !prop.SetKey("origin", arguments[2]) ||
            !prop.SetKey("rendercolor", Color(255, 255, 255, 255)))
        {
            LogWarning("Set keys failed: {}", prop.LastError());
            return;
        }
        bool invoked{};
        if (!prop.DispatchSpawn(invoked))
        {
            LogWarning("Spawn failed: {}", prop.LastError());
            if (invoked) LogWarning("Spawn was invoked; this construction cannot be retried.");
            return;
        }
        LogMessage("Spawned entity {}", prop.Index());
    }
};
}
KEELS2_PLUGIN(docs::EntityCreation)
