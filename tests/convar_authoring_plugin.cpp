#include <keels2/keels2.hpp>

#include <cstdint>
#include <cstring>

namespace
{

std::uint32_t g_loadCount{};
std::uint32_t g_unloadCount{};
std::uint32_t g_callbackCount{};
std::uint32_t g_invalidCount{};
std::uint32_t g_busyCount{};
bool g_unloadInvalid{};
keels2::ConVar<int> g_integerCopy;
keels2::ConVar<int> g_previousCopy;
keels2::ConVar<int> g_foundCopy;
keels2::ConVar<int> g_previousFoundCopy;
keels2::ConVar<int> g_unboundedCopy;
class ConVarAuthoringPlugin;
ConVarAuthoringPlugin* g_plugin{};

class ConVarAuthoringPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "ConVar Authoring Contract",
        "KeelS2 Tests",
        "0.7.0",
        "Validates the C++ ConVar authoring layer"
    };

    bool Load() override
    {
        ++g_loadCount;
        g_previousCopy = g_integerCopy;
        g_previousFoundCopy = g_foundCopy;

        integer = CreateConVar<int>(
            "keels2_authoring_int",
            7,
            "C++ authoring bounded integer",
            FCVAR_NOTIFY,
            1,
            11,
            &ConVarAuthoringPlugin::IntegerChanged);
        floating = CreateConVar<float>(
            "keels2_authoring_float",
            1.5f,
            "C++ authoring bounded float",
            FCVAR_NONE,
            0.5f,
            2.5f);
        boolean = CreateConVar<bool>(
            "keels2_authoring_bool",
            true,
            "C++ authoring boolean");
        string = CreateConVar<CUtlString>(
            "keels2_authoring_string",
            CUtlString("keels2"),
            "C++ authoring string");
        unbounded = CreateConVar<int>(
            "keels2_authoring_unbounded",
            5,
            "C++ authoring unbounded integer");
        limitTeams = FindConVar<int>("MP_LIMITTEAMS");

        g_integerCopy = integer;
        g_foundCopy = limitTeams;
        g_unboundedCopy = unbounded;

        const bool valid = integer && floating && boolean && string &&
            unbounded && limitTeams && integer.GetName() &&
            std::strcmp(integer.GetName(), "keels2_authoring_int") == 0 &&
            integer.HasMin() && integer.HasMax() &&
            integer.Min() == 1 && integer.Max() == 11 &&
            !unbounded.HasMin() && !unbounded.HasMax() &&
            std::strcmp(limitTeams.GetName(), "mp_limitteams") == 0 &&
            g_callbackCount == 0 && integer.Set(100) &&
            integer.Get() == 11 && floating.Set(9.0f) &&
            floating.Get() == 2.5f && g_callbackCount == 0;
        if (!valid)
        {
            ++g_invalidCount;
            return false;
        }

        g_plugin = this;
        LogMessage(
            "loaded int={} float={} bool={} string={} limitteams={}",
            integer.Get(),
            floating.Get(),
            boolean.Get(),
            string.Get().Get(),
            limitTeams.Get());
        return true;
    }

    void Unload() override
    {
        ++g_unloadCount;
        g_unloadInvalid = !integer && !floating && !boolean && !string &&
            !unbounded && !limitTeams && !g_integerCopy && !g_foundCopy &&
            !g_previousCopy && !g_previousFoundCopy && !g_unboundedCopy &&
            integer.Get() == 0 && !integer.Set(5) && !g_integerCopy.Set(5);
        if (!g_unloadInvalid)
        {
            ++g_invalidCount;
        }
        LogMessage("unload invalidation={}", g_unloadInvalid);
        g_plugin = nullptr;
    }

    bool RemoveIntegerCopy()
    {
        return RemoveConVar(g_integerCopy) && !integer && !g_integerCopy;
    }

    bool RemoveFoundCopy()
    {
        return RemoveConVar(g_foundCopy) && !limitTeams && !g_foundCopy;
    }

    bool SetString(const char* value)
    {
        return value && string.Set(CUtlString(value));
    }

    bool StringEquals(const char* value)
    {
        if (!value)
        {
            return false;
        }
        const CUtlString current = string.Get();
        const char* text = current.Get();
        return text && std::strcmp(text, value) == 0;
    }

private:
    void IntegerChanged(
        ConVar<int>& convar,
        CSplitScreenSlot slot,
        int newValue,
        int oldValue)
    {
        ++g_callbackCount;
        if (!convar || std::strcmp(convar.GetName(), "keels2_authoring_int") != 0 ||
            slot.Get() != 0 || !convar.HasMin() || !convar.HasMax() ||
            convar.Min() != 1 || convar.Max() != 11 ||
            convar.Get() != newValue || newValue == oldValue)
        {
            ++g_invalidCount;
        }
        if (newValue == 1 && g_busyCount == 0)
        {
            const bool removed = RemoveConVar(g_integerCopy);
            if (removed || !integer || !g_integerCopy || integer.Get() != newValue ||
                g_integerCopy.Get() != newValue)
            {
                ++g_invalidCount;
            }
            else
            {
                ++g_busyCount;
            }
        }
    }

    ConVar<int> integer;
    ConVar<float> floating;
    ConVar<bool> boolean;
    ConVar<CUtlString> string;
    ConVar<int> unbounded;
    ConVar<int> limitTeams;
};

}

KEELS2_PLUGIN(ConVarAuthoringPlugin)

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_ConVarAuthoringValue(
    std::uint32_t value)
{
    switch (value)
    {
        case 0:
            return g_loadCount;
        case 1:
            return g_unloadCount;
        case 2:
            return g_callbackCount;
        case 3:
            return g_invalidCount;
        case 4:
            return g_unloadInvalid ? 1u : 0u;
        case 5:
            return g_integerCopy ? 1u : 0u;
        case 6:
            return static_cast<std::uint32_t>(g_integerCopy.Get());
        case 7:
            return g_previousCopy ? 1u : 0u;
        case 8:
            return g_foundCopy ? 1u : 0u;
        case 9:
            return g_unboundedCopy.HasMin() || g_unboundedCopy.HasMax() ? 1u : 0u;
        case 10:
            return g_foundCopy &&
                    std::strcmp(g_foundCopy.GetName(), "mp_limitteams") == 0
                ? 1u
                : 0u;
        case 11:
            return g_previousFoundCopy ? 1u : 0u;
        case 12:
            return g_unboundedCopy ? 1u : 0u;
        case 13:
            return g_busyCount;
        default:
            return 0;
    }
}

extern "C" KEELS2_PLUGIN_EXPORT int KeelTest_ConVarAuthoringSet(int value)
{
    return g_integerCopy.Set(value) ? g_integerCopy.Get() : -1;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_ConVarAuthoringRemove(
    std::uint32_t value)
{
    if (!g_plugin)
    {
        return 0;
    }
    if (value == 0)
    {
        return g_plugin->RemoveIntegerCopy() ? 1u : 0u;
    }
    if (value == 1)
    {
        return g_plugin->RemoveFoundCopy() ? 1u : 0u;
    }
    return 0;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_ConVarAuthoringSetString(
    const char* value)
{
    return g_plugin && g_plugin->SetString(value) ? 1u : 0u;
}

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_ConVarAuthoringStringEquals(
    const char* value)
{
    return g_plugin && g_plugin->StringEquals(value) ? 1u : 0u;
}
