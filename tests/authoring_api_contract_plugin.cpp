#include <keels2/keels2.hpp>

#include <type_traits>

class AuthoringApiContractPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Authoring API Contract",
        "KeelS2",
        "0.5D",
        "Pins the canonical native C++ plugin shape"
    };

    bool Load() override
    {
        integer = CreateConVar<int>(
            "keels2_authoring_api_int",
            42,
            "Pins bounded ConVar authoring",
            FCVAR_NOTIFY,
            0,
            100,
            &AuthoringApiContractPlugin::IntegerChanged);
        floating = CreateConVar<float>(
            "keels2_authoring_api_float",
            1.25f,
            "Pins bounded ConVar authoring without a callback",
            FCVAR_NONE,
            0.25f,
            4.0f);
        limitTeams = FindConVar<int>("mp_limitteams");
        const bool command = CreateCommand(
            "keels2_authoring_api_contract",
            "Pins member command registration with raw Source-compatible flags",
            &AuthoringApiContractPlugin::Command);
        return integer && floating && limitTeams && command;
    }

private:
    void Command(const CCommandContext& context, const CCommand& command)
    {
        LogMessage(
            "caller={} command={} int={} float={} limitteams={}",
            context.GetPlayerSlot(),
            command.GetCommandString(),
            integer.Get(),
            floating.Get(),
            limitTeams.Get());
    }

    void IntegerChanged(
        ConVar<int>& convar,
        CSplitScreenSlot slot,
        int newValue,
        int oldValue)
    {
        LogMessage(
            "{} changed slot={} old={} new={}",
            convar.GetName(),
            slot,
            oldValue,
            newValue);
    }

    ConVar<int> integer;
    ConVar<float> floating;
    ConVar<int> limitTeams;
};

static_assert(std::is_base_of_v<keels2::Plugin, AuthoringApiContractPlugin>);
static_assert(std::is_same_v<
    std::remove_cv_t<decltype(AuthoringApiContractPlugin::Info)>,
    keels2::PluginInfo>);
static_assert(std::is_copy_constructible_v<keels2::ConVar<int>>);
static_assert(std::is_copy_assignable_v<keels2::ConVar<int>>);

KEELS2_PLUGIN(AuthoringApiContractPlugin)
