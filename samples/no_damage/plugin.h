#ifndef KEELS2_SAMPLE_NO_DAMAGE_PLUGIN_H
#define KEELS2_SAMPLE_NO_DAMAGE_PLUGIN_H

#include "policy.h"

#include <keels2/keels2.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace keels2::samples::no_damage
{

struct AttackerInfo
{
    std::uint8_t need_init;
    std::uint8_t is_pawn;
    std::uint8_t is_world;
    std::uint8_t reserved;
    std::uint32_t attacker_pawn;
    std::int32_t attacker_slot;
    std::int32_t team_checked;
    std::int32_t team;
};

struct DamageInfo
{
    std::byte prefix[0x38];
    std::uint32_t inflictor;
    std::uint32_t attacker;
    std::uint32_t ability;
    float damage;
    float totalled_damage;
    std::int32_t damage_type;
    std::byte middle[0x98];
    AttackerInfo attacker_info;
};

static_assert(sizeof(AttackerInfo) == 20);
static_assert(offsetof(DamageInfo, inflictor) == 0x38);
static_assert(offsetof(DamageInfo, attacker) == 0x3c);
static_assert(offsetof(DamageInfo, ability) == 0x40);
static_assert(offsetof(DamageInfo, damage) == 0x44);
static_assert(offsetof(DamageInfo, damage_type) == 0x4c);
static_assert(offsetof(DamageInfo, attacker_info) == 0xe8);

class NoDamagePlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 No Player Damage",
        "KeelS2 Project",
        "0.9.0",
        "Blocks direct player weapon damage through a profile-backed detour"
    };

    bool Load() override;

private:
    using DamageSignature = std::int64_t(DamageInfo*, void*);

    keels2::kh::Action OnDamage(
        keels2::kh::Call<std::int64_t>& call,
        DamageInfo* info,
        void* result);
    void Status(const CCommandContext&, const CCommand&);

    std::atomic<std::uint64_t> seen_{};
    std::atomic<std::uint64_t> blocked_{};
    std::atomic<std::uint64_t> invalid_{};
    std::atomic<std::uint64_t> non_player_victim_{};
    std::atomic<std::uint64_t> non_player_source_{};
    std::atomic<std::uint64_t> self_damage_{};
    std::atomic<std::uint64_t> unrelated_damage_{};
    std::atomic<std::uint64_t> result_errors_{};
    std::atomic<std::uint32_t> last_victim_{invalid_handle};
    std::atomic<std::uint32_t> last_attacker_{invalid_handle};
    std::atomic<std::uint32_t> last_weapon_{invalid_handle};
    std::atomic<std::uint32_t> last_source_{invalid_handle};
    std::atomic<std::int32_t> last_damage_type_{};
};

}

#endif
