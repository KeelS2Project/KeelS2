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
std::uint32_t g_observerCount{};
bool g_observerValid{true};
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
        observedTeams = FindConVar<int>("mp_limitteams", &ConVarAuthoringPlugin::TeamsChanged);

        g_integerCopy = integer;
        g_foundCopy = limitTeams;
        g_unboundedCopy = unbounded;

        const bool valid = integer && floating && boolean && string &&
            unbounded && limitTeams && observedTeams && integer.GetName() &&
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

    bool CheckObserver(int stage)
    {
        if (stage == 0)
        {
            const bool initially_valid = observedTeams && g_observerCount == 0;
            const bool changed = observedTeams.Set(3);
            const int current = observedTeams.Get();
            const bool valid = initially_valid && changed && current == 4 &&
                g_observerCount == 2 && g_observerValid;
            if (!valid)
            {
                LogError("observer initial={} set={} current={} callbacks={} valid={}",
                    initially_valid, changed, current, g_observerCount, g_observerValid);
            }
            return valid;
        }
        if (stage == 1)
        {
            if (observedTeams.Get() != 4 || g_observerCount != 2 || !g_observerValid ||
                !observedTeams.Set(2) || g_observerCount != 3 || !RemoveConVar(observedTeams) || observedTeams)
            {
                return false;
            }
            auto plain = FindConVar<int>("mp_limitteams");
            return plain.Set(5) && g_observerCount == 3 && plain.Set(2) && g_observerCount == 3;
        }
        if (stage == 3)
        {
            auto rejected = FindConVar<int>("mp_limitteams", &ConVarAuthoringPlugin::TeamsChanged);
            if (rejected || LastResult() != KEEL_RESULT_WRONG_THREAD)
            {
                return false;
            }
            auto created = CreateConVar<int>("keels2_worker_rejected", 1, "wrong thread");
            return !created && LastResult() == KEEL_RESULT_WRONG_THREAD;
        }
        if (stage == 2)
        {
            observedFloat = FindConVar<float>("keels2_authoring_float", &ConVarAuthoringPlugin::FloatChanged);
            observedBool = FindConVar<bool>("keels2_authoring_bool", &ConVarAuthoringPlugin::BoolChanged);
            observedString = FindConVar<CUtlString>("keels2_authoring_string", &ConVarAuthoringPlugin::StringChanged);
            firstPeer = FindConVar<int>("keels2_authoring_unbounded", &ConVarAuthoringPlugin::FirstChanged);
            secondPeer = FindConVar<int>("keels2_authoring_unbounded", &ConVarAuthoringPlugin::SecondChanged);
            if (!observedFloat || !observedBool || !observedString || !firstPeer || !secondPeer ||
                !floating.Set(1.234567f) || !floating.Set(2.345678f) || floatCount != 2 ||
                !boolean.Set(false) || !boolean.Set(true) || boolCount != 2 ||
                !string.Set(CUtlString("100% {literal}; quit")) ||
                !string.Set(CUtlString("retained")) || stringCount != 2 ||
                !unbounded.Set(17) || firstCount != 1 || secondCount != 0 ||
                !unbounded.Set(18) || firstCount != 2 || secondCount != 0 || !observerValuesValid)
            {
                LogError("typed observers: float={} bool={} string={} first={} second={} valid={}",
                    floatCount, boolCount, stringCount, firstCount, secondCount, observerValuesValid);
                return false;
            }
            return true;
        }
        return false;
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
    void FloatChanged(ConVar<float>& convar, CSplitScreenSlot, float current, float previous)
    {
        ++floatCount;
        const float expectedPrevious = floatCount == 1 ? 2.5f : 1.234567f;
        const float expectedCurrent = floatCount == 1 ? 1.234567f : 2.345678f;
        observerValuesValid = observerValuesValid && previous == expectedPrevious &&
            current == expectedCurrent && convar.Get() == current;
    }

    void BoolChanged(ConVar<bool>& convar, CSplitScreenSlot, bool current, bool previous)
    {
        ++boolCount;
        observerValuesValid = observerValuesValid && previous != current &&
            current == (boolCount == 2) && convar.Get() == current;
    }

    void StringChanged(ConVar<CUtlString>& convar, CSplitScreenSlot, CUtlString current, CUtlString previous)
    {
        ++stringCount;
        const char* expectedPrevious = stringCount == 1 ? "worker" : "100% {literal}; quit";
        const char* expectedCurrent = stringCount == 1 ? "100% {literal}; quit" : "retained";
        observerValuesValid = observerValuesValid && V_strcmp(previous.Get(), expectedPrevious) == 0 &&
            V_strcmp(current.Get(), expectedCurrent) == 0 && convar.Get() == current;
    }

    void FirstChanged(ConVar<int>&, CSplitScreenSlot, int current, int previous)
    {
        ++firstCount;
        observerValuesValid = observerValuesValid && previous == (firstCount == 1 ? 5 : 17) &&
            current == (firstCount == 1 ? 17 : 18) && !RemoveConVar(unbounded) && unbounded;
        if (firstCount == 1)
        {
            observerValuesValid = observerValuesValid && RemoveConVar(secondPeer) && !secondPeer;
        }
    }

    void SecondChanged(ConVar<int>&, CSplitScreenSlot, int, int)
    {
        ++secondCount;
    }

    void TeamsChanged(ConVar<int>& convar, CSplitScreenSlot slot, int newValue, int oldValue)
    {
        ++g_observerCount;
        if (!convar || slot.Get() != 0 || newValue == oldValue || convar.Get() != newValue)
        {
            g_observerValid = false;
        }
        if (newValue == 3)
        {
            if (RemoveConVar(observedTeams) || !observedTeams || !observedTeams.Set(4) ||
                observedTeams.Get() != 3 || g_observerCount != 1)
            {
                g_observerValid = false;
            }
        }
    }

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
    ConVar<int> observedTeams;
    ConVar<float> observedFloat;
    ConVar<bool> observedBool;
    ConVar<CUtlString> observedString;
    ConVar<int> firstPeer;
    ConVar<int> secondPeer;
    int floatCount{};
    int boolCount{};
    int stringCount{};
    int firstCount{};
    int secondCount{};
    bool observerValuesValid{true};
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

extern "C" KEELS2_PLUGIN_EXPORT std::uint32_t KeelTest_ConVarObserverCheck(int stage)
{
    return g_plugin && g_plugin->CheckObserver(stage) ? 1u : 0u;
}
