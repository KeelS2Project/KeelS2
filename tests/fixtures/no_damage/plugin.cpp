#include "plugin.h"

#include <entity2/entityinstance.h>

#include <cstring>

namespace keels2::samples::no_damage
{

bool NoDamagePlugin::Load()
{
    if (!HookProfilePre<DamageSignature>(
            "cs2.base_entity.take_damage",
            &NoDamagePlugin::OnDamage) ||
        !CreateCommand(
            "keel_no_damage_status",
            "Shows no-player-damage hook counters and the last classified hit",
            &NoDamagePlugin::Status))
    {
        LogError("profile-backed damage hook registration failed");
        return false;
    }
    LogMessage(
        "ready target=cs2.base_entity.take_damage policy=direct-player-weapons");
    return true;
}

keels2::kh::Action NoDamagePlugin::OnDamage(
    keels2::kh::Call<std::int64_t>& call,
    DamageInfo* info,
    void*)
{
    seen_.fetch_add(1, std::memory_order_relaxed);
    auto* victim = call.Instance<CEntityInstance>();
    const bool valid = victim && victim->m_pEntity && info;
    const char* classname = valid ? victim->GetClassname() : nullptr;
    const std::uint32_t victim_handle = valid
        ? static_cast<std::uint32_t>(victim->GetRefEHandle().ToInt())
        : invalid_handle;
    const Input input{
        valid,
        classname && std::strcmp(classname, "player") == 0,
        valid && info->attacker_info.is_pawn != 0,
        valid && info->attacker_info.is_world != 0,
        victim_handle,
        valid ? info->attacker : invalid_handle,
        valid ? info->attacker_info.attacker_pawn : invalid_handle,
        valid ? info->damage_type : 0
    };
    if (valid)
    {
        last_victim_.store(victim_handle, std::memory_order_relaxed);
        last_attacker_.store(info->attacker, std::memory_order_relaxed);
        last_weapon_.store(info->ability, std::memory_order_relaxed);
        last_source_.store(info->inflictor, std::memory_order_relaxed);
        last_damage_type_.store(info->damage_type, std::memory_order_relaxed);
    }
    switch (Classify(input))
    {
        case Decision::block:
            if (!call.SetResult<std::int64_t>(1))
            {
                result_errors_.fetch_add(1, std::memory_order_relaxed);
                return keels2::kh::Action::Continue;
            }
            blocked_.fetch_add(1, std::memory_order_relaxed);
            return keels2::kh::Action::Supersede;
        case Decision::invalid:
            invalid_.fetch_add(1, std::memory_order_relaxed);
            break;
        case Decision::non_player_victim:
            non_player_victim_.fetch_add(1, std::memory_order_relaxed);
            break;
        case Decision::non_player_source:
            non_player_source_.fetch_add(1, std::memory_order_relaxed);
            break;
        case Decision::self_damage:
            self_damage_.fetch_add(1, std::memory_order_relaxed);
            break;
        case Decision::unrelated_damage:
            unrelated_damage_.fetch_add(1, std::memory_order_relaxed);
            break;
    }
    return keels2::kh::Action::Continue;
}

void NoDamagePlugin::Status(const CCommandContext&, const CCommand&)
{
    LogMessage(
        "status ready=true seen={} blocked={} invalid={} non_player_victim={} "
        "non_player_source={} self={} unrelated={} result_errors={} "
        "last_victim={} last_attacker={} last_weapon={} last_source={} "
        "last_damage_type={}",
        seen_.load(std::memory_order_relaxed),
        blocked_.load(std::memory_order_relaxed),
        invalid_.load(std::memory_order_relaxed),
        non_player_victim_.load(std::memory_order_relaxed),
        non_player_source_.load(std::memory_order_relaxed),
        self_damage_.load(std::memory_order_relaxed),
        unrelated_damage_.load(std::memory_order_relaxed),
        result_errors_.load(std::memory_order_relaxed),
        last_victim_.load(std::memory_order_relaxed),
        last_attacker_.load(std::memory_order_relaxed),
        last_weapon_.load(std::memory_order_relaxed),
        last_source_.load(std::memory_order_relaxed),
        last_damage_type_.load(std::memory_order_relaxed));
}

}

KEELS2_PLUGIN(keels2::samples::no_damage::NoDamagePlugin)
