#ifndef KEELS2_SAMPLE_PLUGIN_H
#define KEELS2_SAMPLE_PLUGIN_H

#include <keels2/authoring.hpp>

namespace sample
{
using namespace keels2::authoring;

class SamplePlugin final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        .name = "KeelS2 Sample",
        .author = "KeelS2 Project",
        .version = "1.2.0",
        .description = "Commands, players, text, ConVars, events, and native hooks"
    };

    bool Load() override;

private:
    void Command(const CCommandContext& context, const CCommand& command);
    void DescribePlayer(const PlayerConnection& connection);
    int CountPlayers();
    void ConVarChanged(ConVar<int32>& convar, CSplitScreenSlot slot, int32 newValue, int32 oldValue);
    void OnRoundStart(IGameEvent* event);
    Action OnCommand(ConCommandRef reference, const CCommandContext& context, const CCommand& command);

    ConVar<int32> integer;
    ConVar<int32> limitTeams;
};
}

#endif
