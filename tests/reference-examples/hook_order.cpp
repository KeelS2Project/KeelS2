#include <keels2/authoring.hpp>
#include "math_service.h"

namespace docs
{
using namespace keels2::authoring;

class OrderedHook final : public Plugin
{
public:
    static constexpr PluginInfo Info{DOCS_HOOK_LABEL, "KeelS2 documentation", "1.0.0",
        "Observe one shared target from two plugins"};

    bool Load() override
    {
        const void* service = nullptr;

        if (HostContext().QueryService(DOCS_MATH_NAME, DOCS_MATH_VERSION, &service) != KEEL_RESULT_OK)
            return false;

        const auto* math = static_cast<const DocsMathService*>(service);

        if (!math || math->size != sizeof(*math) || math->version != DOCS_MATH_VERSION || !math->add)
            return false;

        const auto address = keels2::kh::TargetSpec::Address(reinterpret_cast<void*>(math->add));
        return hooks.Connect(HostContext()) == KEEL_RESULT_OK
            && hooks.Resolve<std::int32_t(std::int32_t, std::int32_t)>(address, target) == KEEL_RESULT_OK
            && hooks.AddCallback<std::int32_t(std::int32_t, std::int32_t), &OrderedHook::Observe>(
                target, callback, keels2::kh::Phase::Both, DOCS_HOOK_PRIORITY, *this) == KEEL_RESULT_OK;
    }

private:
    Action Observe(HookCall<std::int32_t>& call, std::int32_t left, std::int32_t right)
    {
        LogMessage("{} {}: {} + {}", DOCS_HOOK_LABEL,
            call.CurrentPhase() == keels2::kh::Phase::Pre ? "pre" : "post", left, right);

        return PLUGIN_CONTINUE;
    }

    keels2::kh::Service hooks;
    keels2::kh::Target target;
    keels2::kh::Callback callback;
};
}

KEELS2_PLUGIN(docs::OrderedHook)
