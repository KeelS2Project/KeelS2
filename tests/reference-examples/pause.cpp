#include <keels2/authoring.hpp>
#include <cstring>

using namespace keels2::authoring;

class PauseGuard final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Pause Guard", "KeelS2 documentation", "1.0.0", "Controlled pause preparation"};
    bool Load() override
    {
        return CreateCommand("pause_guard", "Allow or refuse the next pause", &PauseGuard::Command);
    }
    bool PreparePause() override
    {
        if (!ready_) LogMessage("Pause refused. Run pause_guard allow when the plugin is ready.");
        return ready_;
    }
    void OnPluginResumed(const PluginSnapshot& plugin) override
    {
        if (plugin.id == HostContext().PluginHandle()) LogMessage("Pause Guard resumed.");
    }
private:
    bool ready_ = false;
    void Command(const CCommandContext& context, const CCommand& command)
    {
        if (context.GetPlayerSlot().Get() != -1) return;
        if (command.ArgC() != 2 || (std::strcmp(command[1], "allow") != 0 && std::strcmp(command[1], "refuse") != 0))
        {
            LogMessage("Usage: pause_guard <allow|refuse>");
            return;
        }
        ready_ = std::strcmp(command[1], "allow") == 0;
        if (ready_) LogMessage("The next pause is allowed.");
        else LogMessage("The next pause will be refused.");
    }
};

KEELS2_PLUGIN(PauseGuard)
