#ifndef KEELS2_NATIVE_AUTHORING_FIXTURE_H
#define KEELS2_NATIVE_AUTHORING_FIXTURE_H

#include <keels2/authoring.hpp>

namespace native_authoring_fixture
{

using keels2::authoring::Action;
using keels2::authoring::Entity;
using keels2::authoring::HookCall;
using keels2::authoring::SchemaField;
using keels2::authoring::PLUGIN_CONTINUE;
using keels2::authoring::PLUGIN_OVERRIDE;
using keels2::authoring::PLUGIN_SUPERSEDE;

struct NativeCalls
{
    int commands{};
    int voices{};
    ConCommandRef reference;
    const CCommandContext* context{};
    const CCommand* command{};
    bool valid = true;
};

void CommandTarget(ConCommandRef, const CCommandContext&, const CCommand&);
bool VoiceTarget(ConCommandRef, CPlayerSlot, bool);
void InvokeCommand(ConCommandRef, const CCommandContext&, const CCommand&);
bool InvokeVoice(ConCommandRef, CPlayerSlot, bool);
NativeCalls& Calls();

class SourceRootStyle
{
public:
    Action Dispatch(ConCommandRef reference,
        const CCommandContext& context, const CCommand& command);
    Action Listening(HookCall<bool>& call,
        ConCommandRef& reference, CPlayerSlot slot, bool listening);
    void CommandPeer(ConCommandRef reference,
        const CCommandContext& context, const CCommand& command);
    void VoicePeer(ConCommandRef reference, CPlayerSlot slot, bool listening);
    void CommandPost(ConCommandRef, const CCommandContext&, const CCommand&);
    void VoicePost(ConCommandRef, CPlayerSlot, bool);

    Action action = PLUGIN_CONTINUE;
    int command_pre{};
    int voice_pre{};
    int command_peer{};
    int voice_peer{};
    int command_post{};
    int voice_post{};
    bool valid = true;
    SchemaField<CEntityHandle> pawn_field;
    Entity pawn;
};

bool Check(const KeelHookApi& api, KeelPluginHandle plugin);

}

#endif
