#include "native_authoring_fixture.h"

namespace native_authoring_fixture
{

namespace
{
NativeCalls calls;
}

void CommandTarget(ConCommandRef reference,
    const CCommandContext& context, const CCommand& command)
{
    ++calls.commands;
    calls.reference = reference;
    calls.context = &context;
    calls.command = &command;
    calls.valid = calls.valid && reference.GetAccessIndex() == 0x1234 &&
        reference.GetRegisteredIndex() == 0x76543210;
}

bool VoiceTarget(ConCommandRef reference, CPlayerSlot slot, bool listening)
{
    ++calls.voices;
    calls.reference = reference;
    return reference.GetAccessIndex() == 0x2345 &&
        reference.GetRegisteredIndex() == 0x12345678 &&
        slot.Get() == 7 && listening;
}

void InvokeCommand(ConCommandRef reference,
    const CCommandContext& context, const CCommand& command)
{
    auto* volatile target = &CommandTarget;
    target(reference, context, command);
}

bool InvokeVoice(ConCommandRef reference, CPlayerSlot slot, bool listening)
{
    auto* volatile target = &VoiceTarget;
    return target(reference, slot, listening);
}

NativeCalls& Calls()
{
    return calls;
}

}
