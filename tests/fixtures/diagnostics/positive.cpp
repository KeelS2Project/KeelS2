#include "positive.h"

bool Example::Load()
{
    return CreateCommand("example", "help", &Example::Command) &&
        ListenForGameEvent("round_start", &Example::Event) &&
        HookPre(GetCVarSystem<ICvar>(), &ICvar::DispatchConCommand, &Example::OnCommand);
}

Action Example::OnCommand(ConCommandRef, const CCommandContext&, const CCommand&)
{
    return PLUGIN_CONTINUE;
}

Action Example::OnVoice(HookCall<bool>&, CPlayerSlot, bool)
{
    return PLUGIN_OVERRIDE;
}

void Example::Command(const CCommandContext&, const CCommand&)
{
}

void Example::Event(IGameEvent*)
{
}

KEELS2_PLUGIN(Example)
