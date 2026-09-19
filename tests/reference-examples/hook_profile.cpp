#include <keels2/authoring.hpp>
#include <cstdint>

namespace docs
{
using namespace keels2::authoring;
// The accepted CS2 profile describes an opaque DamageInfo pointer plus result storage.
// This observation example never reads the pointed-to layout.
struct DamageInfo;

class ProfileHook final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        "Docs Profile Hook", "KeelS2 documentation", "1.0.0", "Observe a profile-backed damage target"};

    bool Load() override
    {
        using Signature = std::int64_t(DamageInfo*, void*);
        return HookProfilePre<Signature>("cs2.base_entity.take_damage", &ProfileHook::Before) &&
            HookProfilePost<Signature>("cs2.base_entity.take_damage", &ProfileHook::After);
    }

private:
    Action Before(DamageInfo*, void*)
    {
        ++observed;
        return PLUGIN_CONTINUE;
    }

    Action After(HookCall<std::int64_t>& call, DamageInfo*, void*)
    {
        if (observed == 1 && call.OriginalCalled())
        {
            const auto result = call.Result();

            if (result)
                LogMessage("First profile dispatch returned {}", *result);
        }

        return PLUGIN_CONTINUE;
    }

    std::uint64_t observed{};
};
}

KEELS2_PLUGIN(docs::ProfileHook)
