#include <keels2/authoring.hpp>
#include "hook_target.h"

namespace docs
{
using namespace keels2::authoring;

class Arguments final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        "Docs Hook Arguments", "KeelS2 documentation", "1.0.0", "Change an argument to a plugin-owned virtual call"};

    bool Load() override
    {
        return HookPre(&calculator, &Calculator::Calculate, &Arguments::Before, 10)
            && CreateCommand("keel_docs_hook_args", "Run the controlled hook example", &Arguments::Run);
    }

private:
    Action Before(int& value)
    {
        value += 3;
        return PLUGIN_CONTINUE;
    }

    void Run(const CCommandContext&, const CCommand&)
    {
        LogMessage("Calculate(4), after +3 argument change: {}", InvokeCalculator(&calculator, 4));
    }

    Calculator calculator;
};
}

KEELS2_PLUGIN(docs::Arguments)
