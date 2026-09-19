#include <keels2/authoring.hpp>
#include <cstring>

namespace docs
{
using namespace keels2::authoring;

class UnloadGuard final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Unload Guard", "KeelS2 documentation", "1.0.0",
        "Refuse unload until the plugin is ready"};

    bool Load() override
    {
        ready = false;
        return CreateCommand("keel_docs_unload", "Allow or refuse unload preparation", &UnloadGuard::Command);
    }

    bool PrepareUnload() override
    {
        if (!ready)
            LogMessage("Unload refused. Run keel_docs_unload allow before retrying.");

        return ready;
    }

private:
    void Command(const CCommandContext& context, const CCommand& command)
    {
        if (context.GetPlayerSlot().Get() != -1)
            return;

        if (command.ArgC() != 2 || (std::strcmp(command[1], "allow") != 0 && std::strcmp(command[1], "refuse") != 0))
        {
            LogMessage("Usage: keel_docs_unload <allow|refuse>");
            return;
        }

        ready = std::strcmp(command[1], "allow") == 0;
        LogMessage("Unload preparation is {}.", ready ? "allowed" : "refused");
    }

    bool ready = false;
};
}

KEELS2_PLUGIN(docs::UnloadGuard)
