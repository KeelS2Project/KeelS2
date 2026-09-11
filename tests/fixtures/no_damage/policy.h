#ifndef KEELS2_SAMPLE_NO_DAMAGE_POLICY_H
#define KEELS2_SAMPLE_NO_DAMAGE_POLICY_H

#include <cstdint>

namespace keels2::samples::no_damage
{

enum class Decision
{
    block,
    invalid,
    non_player_victim,
    non_player_source,
    self_damage,
    unrelated_damage
};

struct Input
{
    bool valid;
    bool player_victim;
    bool attacker_is_pawn;
    bool attacker_is_world;
    std::uint32_t victim;
    std::uint32_t attacker;
    std::uint32_t attacker_pawn;
    std::int32_t damage_type;
};

constexpr std::uint32_t invalid_handle = 0xffffffffu;
constexpr std::int32_t direct_weapon_damage =
    (1 << 1) | (1 << 2) | (1 << 7) | (1 << 8) | (1 << 11);
constexpr std::int32_t unrelated_damage =
    (1 << 0) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) |
    (1 << 9) | (1 << 10) | (1 << 14) | (1 << 16) | (1 << 17) |
    (1 << 18) | (1 << 22) | (1 << 23) | (1 << 24);

constexpr bool ValidHandle(std::uint32_t handle) noexcept
{
    return handle != invalid_handle;
}

constexpr Decision Classify(const Input& input) noexcept
{
    if (!input.valid)
    {
        return Decision::invalid;
    }
    if (!input.player_victim)
    {
        return Decision::non_player_victim;
    }
    if (!input.attacker_is_pawn || input.attacker_is_world ||
        !ValidHandle(input.attacker) || !ValidHandle(input.attacker_pawn))
    {
        return Decision::non_player_source;
    }
    if (input.attacker == input.victim || input.attacker_pawn == input.victim)
    {
        return Decision::self_damage;
    }
    if ((input.damage_type & direct_weapon_damage) == 0 ||
        (input.damage_type & unrelated_damage) != 0)
    {
        return Decision::unrelated_damage;
    }
    return Decision::block;
}

}

#endif
