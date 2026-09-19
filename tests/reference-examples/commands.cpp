#include <keels2/authoring.hpp>
#include <string_view>

namespace docs
{
using namespace keels2::authoring;

class Commands final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        "Docs Commands", "KeelS2 documentation", "1.0.0", "A managed command and bounded ConVar"};

    bool Load() override
    {
        count = CreateConVar<int32>("keel_docs_count", 1, "Greeting count", FCVAR_NOTIFY,
            1, 5, &Commands::Changed);

        if (!count || !CreateCommand("keel_docs_greet", "Print a greeting", &Commands::Greet))
        {
            LogError("Setup failed: {}", LastError());
            return false;
        }

        return CreateCommand("keel_docs_reset", "Reset or retire the greeting registrations", &Commands::Reset);
    }

private:
    void Greet(const CCommandContext& context, const CCommand& command)
    {
        if (command.ArgC() != 1)
        {
            LogWarning("Usage: keel_docs_greet");
            return;
        }

        int32 value{};

        if (!count.Read(value))
        {
            LogError("{}: {}", count.GetName(), count.LastError());
            return;
        }

        for (int32 i = 0; i < value; ++i)
            LogMessage("Hello from slot {}", context.GetPlayerSlot());

        if (count.HasMax() && count.HasMin())
            LogMessage("Range {}..{}; current {}", count.Min(), count.Max(), count.Get());
    }

    void Reset(const CCommandContext&, const CCommand& command)
    {
        if (command.ArgC() == 2 && std::string_view(command[1]) == "retire")
        {
            if (!RemoveCommand("keel_docs_greet") || !RemoveConVar(count))
                LogWarning("Removal: {}", LastError());

            return;
        }

        auto existing = FindConVar<int32>("keel_docs_count");

        if (!existing || !existing.Set(1))
            LogWarning("Reset failed: {}", existing.LastError());
    }

    void Changed(ConVar<int32>& convar, CSplitScreenSlot slot, int32 next, int32 previous)
    {
        LogMessage("{} [{}]: {} -> {}", convar.GetName(), slot, previous, next);
    }

    ConVar<int32> count;
};
}

KEELS2_PLUGIN(docs::Commands)
