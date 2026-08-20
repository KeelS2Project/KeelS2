#include "plugin.h"

#include <cstring>

bool BasicPlugin::Load()
{
    return CreateCommand(
        "keel_test",
        "Verifies the KeelS2 native plugin command path",
        &BasicPlugin::TestCommand);
}

void BasicPlugin::Unload()
{
    LogMessage("unload callback completed");
}

void BasicPlugin::TestCommand(const CCommandContext& context, const CCommand& command)
{
    static_cast<void>(context);
    if (command.ArgC() == 2 &&
        std::strcmp(command[1], "unregister") == 0)
    {
        if (RemoveCommand("keel_test"))
        {
            LogMessage("keel_test command unregistered.");
        }
        else
        {
            LogError("Could not unregister the keel_test command.");
        }
        return;
    }
    LogMessage("KeelS2 0.1.0-dev is active. The basic native plugin is responding.");
}

KEELS2_PLUGIN(BasicPlugin)
