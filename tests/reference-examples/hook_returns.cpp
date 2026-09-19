#include <keels2/authoring.hpp>
#include "hook_target.h"

namespace docs
{
using namespace keels2::authoring;

class Returns final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Hook Returns",
                                     "KeelS2 documentation",
                                     "1.0.0",
                                     "Compare return override and supersede on an owned target"};

    bool Load() override
    {
        return HookPre(&calculator, &Calculator::Calculate, &Returns::Before)
            && HookPost(&calculator, &Calculator::Calculate, &Returns::After)
            && CreateCommand("keel_docs_hook_returns", "Run return-value examples", &Returns::Run);
    }

private:
    Action Before(HookCall<int>& call, int value)
    {
        if (value == 0 && call.SetResult(100))
            return PLUGIN_SUPERSEDE;

        return PLUGIN_CONTINUE;
    }

    Action After(HookCall<int>& call, int value)
    {
        const auto result = call.Result();
        LogMessage("value={} original_called={} result={}", value, call.OriginalCalled(), result.value_or(-1));

        if (value == 1 && call.SetResult(200))
            return PLUGIN_OVERRIDE;

        return PLUGIN_CONTINUE;
    }

    void Run(const CCommandContext&, const CCommand&)
    {
        for (int value = 0; value < 3; ++value)
            LogMessage("Calculate({}) => {}", value, InvokeCalculator(&calculator, value));
    }

    Calculator calculator;
};
}

KEELS2_PLUGIN(docs::Returns)
