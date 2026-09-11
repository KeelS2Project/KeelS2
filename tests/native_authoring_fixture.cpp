#include "native_authoring_fixture.h"

#include <array>
#include <bit>
#include <cstring>
#include <type_traits>

namespace native_authoring_fixture
{

static_assert(std::is_same_v<Action, keels2::kh::Action>);
static_assert(static_cast<KeelHookAction>(PLUGIN_CONTINUE) == KH_ACTION_CONTINUE);
static_assert(static_cast<KeelHookAction>(PLUGIN_OVERRIDE) == KH_ACTION_OVERRIDE);
static_assert(static_cast<KeelHookAction>(PLUGIN_SUPERSEDE) == KH_ACTION_SUPERSEDE);
static_assert(std::is_same_v<Entity, keels2::Entity>);
static_assert(std::is_same_v<SchemaField<CEntityHandle>, keels2::SchemaField<CEntityHandle>>);

namespace
{

bool OriginalReference(ConCommandRef reference)
{
    return reference.GetAccessIndex() == 0x1234 &&
        reference.GetRegisteredIndex() == 0x76543210;
}

bool CommandArguments(ConCommandRef reference,
    const CCommandContext& context, const CCommand& command)
{
    return OriginalReference(reference) && context.GetPlayerSlot().Get() == 7 &&
        command.ArgC() == 3 && std::strcmp(command[0], "say_team") == 0 &&
        std::strcmp(command[1], "/sr_slap") == 0 &&
        std::strcmp(command[2], "100% {player}; status") == 0;
}

class Registration
{
public:
    Registration(const KeelHookApi& api, KeelPluginHandle plugin)
        : api_(api), plugin_(plugin)
    {
    }

    ~Registration()
    {
        Reset();
    }

    bool Reset()
    {
        bool valid = true;
        for (auto& callback : callbacks_)
        {
            if (callback)
            {
                valid = api_.remove_callback(plugin_, callback) == KEEL_RESULT_OK && valid;
                callback = 0;
            }
        }
        if (target_)
        {
            valid = api_.release_target(plugin_, target_) == KEEL_RESULT_OK && valid;
            target_ = 0;
        }
        return valid;
    }

    template <typename Signature>
    bool Resolve(Signature* function)
    {
        void* address{};
        static_assert(sizeof(address) == sizeof(function));
        std::memcpy(&address, &function, sizeof(address));
        KeelHookTargetSpec spec{};
        spec.size = sizeof(spec);
        spec.source = KH_TARGET_ADDRESS;
        spec.mechanism = KH_MECHANISM_DETOUR;
        spec.address = address;
        return api_.resolve_target(plugin_, &spec,
            &keels2::kh::Prototype<Signature>::value, &target_) == KEEL_RESULT_OK;
    }

    template <typename Signature, auto Method>
    bool Add(SourceRootStyle& owner, KeelHookPhase phase, int priority)
    {
        if (count_ == callbacks_.size())
        {
            return false;
        }
        KeelHookCallbackSpec spec{};
        spec.size = sizeof(spec);
        spec.phases = phase;
        spec.priority = priority;
        spec.callback = &keels2::kh::detail::TypedCallback<
            Signature, false, Method, SourceRootStyle>::Dispatch;
        spec.user_data = &owner;
        return api_.add_callback(plugin_, target_, &spec, &callbacks_[count_++]) ==
            KEEL_RESULT_OK;
    }

private:
    const KeelHookApi& api_;
    KeelPluginHandle plugin_{};
    KeelHookTargetHandle target_{};
    std::array<KeelHookCallbackHandle, 3> callbacks_{};
    std::size_t count_{};
};

}

Action SourceRootStyle::Dispatch(ConCommandRef reference,
    const CCommandContext& context, const CCommand& command)
{
    ++command_pre;
    valid = valid && CommandArguments(reference, context, command);
    return action;
}

Action SourceRootStyle::Listening(HookCall<bool>& call,
    ConCommandRef& reference, CPlayerSlot slot, bool listening)
{
    ++voice_pre;
    valid = valid && OriginalReference(reference) && slot.Get() == 7 && listening;
    reference = ConCommandRef(0x2345, 0x12345678);
    if (action != PLUGIN_CONTINUE)
    {
        valid = call.SetResult(false) && valid;
    }
    return action;
}

void SourceRootStyle::CommandPeer(ConCommandRef reference,
    const CCommandContext& context, const CCommand& command)
{
    ++command_peer;
    valid = valid && CommandArguments(reference, context, command);
}

void SourceRootStyle::VoicePeer(ConCommandRef reference, CPlayerSlot slot, bool listening)
{
    ++voice_peer;
    valid = valid && reference.GetAccessIndex() == 0x2345 &&
        reference.GetRegisteredIndex() == 0x12345678 && slot.Get() == 7 && listening;
}

void SourceRootStyle::CommandPost(ConCommandRef, const CCommandContext&, const CCommand&)
{
    ++command_post;
}

void SourceRootStyle::VoicePost(ConCommandRef, CPlayerSlot, bool)
{
    ++voice_post;
}

bool Check(const KeelHookApi& api, KeelPluginHandle plugin)
{
    using CommandSignature = void(ConCommandRef, const CCommandContext&, const CCommand&);
    using VoiceSignature = bool(ConCommandRef, CPlayerSlot, bool);
    SourceRootStyle owner;
    Registration commands(api, plugin);
    Registration voice(api, plugin);
    if (!commands.Resolve(&CommandTarget) || !voice.Resolve(&VoiceTarget) ||
        !commands.Add<CommandSignature, &SourceRootStyle::Dispatch>(owner, KH_PHASE_PRE, 100) ||
        !commands.Add<CommandSignature, &SourceRootStyle::CommandPeer>(owner, KH_PHASE_PRE, -100) ||
        !commands.Add<CommandSignature, &SourceRootStyle::CommandPost>(owner, KH_PHASE_POST, 0) ||
        !voice.Add<VoiceSignature, &SourceRootStyle::Listening>(owner, KH_PHASE_PRE, 100) ||
        !voice.Add<VoiceSignature, &SourceRootStyle::VoicePeer>(owner, KH_PHASE_PRE, -100) ||
        !voice.Add<VoiceSignature, &SourceRootStyle::VoicePost>(owner, KH_PHASE_POST, 0))
    {
        return false;
    }

    const auto reference = std::bit_cast<ConCommandRef>(std::uint64_t{0x7654321000001234});
    KeelHookValue value{};
    value.type = KH_VALUE_UINT64;
    using Adapter = keels2::kh::ValueAdapter<ConCommandRef>;
    if (!Adapter::Write(value, reference) || value.reserved != 0 ||
        value.scalar.uint64 != 0x7654321000001234 ||
        !OriginalReference(Adapter::Read(value)) || Adapter::Fallback().IsValidRef())
    {
        return false;
    }

    const CCommandContext context(CommandTarget_t::CT_FIRST_SPLITSCREEN_CLIENT, CPlayerSlot(7));
    const char* arguments[] = {"say_team", "/sr_slap", "100% {player}; status"};
    const CCommand command(3, arguments);
    Calls() = {};
    constexpr std::array actions{PLUGIN_CONTINUE, PLUGIN_OVERRIDE, PLUGIN_SUPERSEDE};
    int dispatched{};
    int expected_originals{};
    for (const Action action : actions)
    {
        owner.action = action;
        ++dispatched;
        if (action != PLUGIN_SUPERSEDE)
        {
            ++expected_originals;
        }
        InvokeCommand(reference, context, command);
        const bool listening = InvokeVoice(reference, CPlayerSlot(7), true);
        if (!owner.valid || !Calls().valid || listening != (action == PLUGIN_CONTINUE) ||
            owner.command_pre != dispatched || owner.voice_pre != dispatched ||
            owner.command_peer != dispatched || owner.voice_peer != dispatched ||
            owner.command_post != dispatched || owner.voice_post != dispatched ||
            Calls().commands != expected_originals || Calls().voices != expected_originals ||
            Calls().context != &context || Calls().command != &command)
        {
            return false;
        }
    }
    if (!commands.Reset() || !voice.Reset())
    {
        return false;
    }
    InvokeCommand(reference, context, command);
    return !InvokeVoice(reference, CPlayerSlot(7), true) &&
        Calls().commands == expected_originals + 1 && Calls().voices == expected_originals + 1 &&
        owner.command_pre == dispatched && owner.voice_pre == dispatched && Calls().valid;
}

}
