#include <keels2/authoring.hpp>

namespace docs
{
using namespace keels2::authoring;

class Observe final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        "Docs Hook Observer", "KeelS2 documentation", "1.0.0", "Observe a real engine command dispatch"};

    bool Load() override
    {
        auto* cvars = GetCVarSystem<ICvar>();

        if (!cvars || !HookPre(cvars, &ICvar::DispatchConCommand, &Observe::Command, 0))
        {
            LogError("Command hook: {}", LastError());
            return false;
        }

        return true;
    }

private:
    Action Command(ConCommandRef, const CCommandContext&, const CCommand& command)
    {
        if (command.ArgC() && V_strcmp(command[0], "keel_docs_greet") == 0)
            LogMessage("Observed keel_docs_greet before dispatch.");

        return PLUGIN_CONTINUE;
    }
};
}

KEELS2_PLUGIN(docs::Observe)
