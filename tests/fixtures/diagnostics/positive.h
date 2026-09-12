#ifndef KEELS2_DIAGNOSTIC_POSITIVE_H
#define KEELS2_DIAGNOSTIC_POSITIVE_H

#include <keels2/authoring.hpp>

using namespace keels2::authoring;

class Example : public Plugin
{
public:
    static constexpr PluginInfo Info{
        .name = "Diagnostic contract",
        .author = "KeelS2",
        .version = "1.1.0",
        .description = "Compile diagnostic"
    };
    static constexpr PluginRequirement Requirements[]{
        {.name = "Core Plugin", .version = "1.0.0"}
    };
    bool Load() override;
    Action OnCommand(ConCommandRef, const CCommandContext&, const CCommand&);
    Action OnVoice(HookCall<bool>&, CPlayerSlot, bool);
    void Command(const CCommandContext&, const CCommand&);
    void Event(IGameEvent*);
    ConVar<int32> value;
    Entity entity;
    PlayerInfo player;
    PlayerConnection connection;
    SchemaField<CEntityHandle> pawn;
};

#endif
