#include <keels2/authoring.hpp>
#include <cstdint>

extern "C" std::int32_t DocsDouble(std::int32_t value);

namespace docs
{
using namespace keels2::authoring;

class HookControl final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Hook Control", "KeelS2 documentation", "1.0.0",
        "Original calls, recall and callback removal on an owned inline target"};

    bool Load() override
    {
        const auto address = keels2::kh::TargetSpec::Address(reinterpret_cast<void*>(&DocsDouble));
        return hooks.Connect(HostContext()) == KEEL_RESULT_OK
            && hooks.Resolve<std::int32_t(std::int32_t)>(address, target) == KEEL_RESULT_OK
            && hooks.AddCallback<&HookControl::Control>(target, control, KH_PHASE_PRE, 20, *this) == KEEL_RESULT_OK
            && hooks.AddCallback<&HookControl::Observe>(target, observer, KH_PHASE_PRE | KH_PHASE_POST, 0, *this) == KEEL_RESULT_OK
            && CreateCommand("keel_docs_hook_control", "Run the owned inline target", &HookControl::Run);
    }

private:
    Action Control(keels2::kh::Frame& frame)
    {
        const auto value = frame.Argument<std::int32_t>(0);
        KeelResult result = KEEL_RESULT_OK;

        if (value == 2 && frame.SetArgument(0, std::int32_t{5}))
            result = hooks.CallOriginal(frame);
        else if (value == 3 && frame.SetArgument(0, std::int32_t{7}))
            result = hooks.Recall(frame);

        if (result != KEEL_RESULT_OK)
            LogWarning("Original call or recall failed: {}", result);

        return PLUGIN_CONTINUE;
    }

    Action Observe(keels2::kh::Frame& frame)
    {
        if (frame.Phase() == KH_PHASE_PRE)
        {
            if (frame.Recalled())
                LogMessage("Observer entered the recalled chain.");

            if (frame.Argument<std::int32_t>(0) == 9 && observer.Reset() != KEEL_RESULT_OK)
                LogWarning("Observer removal failed.");
        }

        return PLUGIN_CONTINUE;
    }

    void Run(const CCommandContext&, const CCommand&)
    {
        for (const std::int32_t value : {2, 3, 9, 9})
            LogMessage("DocsDouble({}) => {}", value, DocsDouble(value));
    }

    keels2::kh::Service hooks;
    keels2::kh::Target target;
    keels2::kh::Callback control, observer;
};
}

KEELS2_PLUGIN(docs::HookControl)
