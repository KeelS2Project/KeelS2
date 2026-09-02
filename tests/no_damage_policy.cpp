#include "../samples/no_damage/policy.h"

using keels2::samples::no_damage::Classify;
using keels2::samples::no_damage::Decision;
using keels2::samples::no_damage::Input;
using keels2::samples::no_damage::invalid_handle;

namespace
{

constexpr Input Base(std::int32_t damage_type)
{
    return {true, true, true, false, 100, 200, 200, damage_type};
}

}

int main()
{
    Input input = Base(1 << 1);
    if (Classify(input) != Decision::block)
    {
        return 1;
    }
    input = Base((1 << 2) | (1 << 7));
    if (Classify(input) != Decision::block)
    {
        return 2;
    }
    input = Base(1 << 8);
    if (Classify(input) != Decision::block)
    {
        return 3;
    }
    input = Base(1 << 11);
    if (Classify(input) != Decision::block)
    {
        return 4;
    }
    input = Base(1 << 5);
    if (Classify(input) != Decision::unrelated_damage)
    {
        return 5;
    }
    input = Base(1 << 6);
    if (Classify(input) != Decision::unrelated_damage)
    {
        return 6;
    }
    input = Base((1 << 1) | (1 << 6));
    if (Classify(input) != Decision::unrelated_damage)
    {
        return 7;
    }
    input = Base(1 << 1);
    input.attacker_is_world = true;
    if (Classify(input) != Decision::non_player_source)
    {
        return 8;
    }
    input = Base(1 << 1);
    input.attacker_is_pawn = false;
    if (Classify(input) != Decision::non_player_source)
    {
        return 9;
    }
    input = Base(1 << 1);
    input.attacker = input.victim;
    if (Classify(input) != Decision::self_damage)
    {
        return 10;
    }
    input = Base(1 << 1);
    input.attacker_pawn = input.victim;
    if (Classify(input) != Decision::self_damage)
    {
        return 11;
    }
    input = Base(1 << 1);
    input.attacker = invalid_handle;
    if (Classify(input) != Decision::non_player_source)
    {
        return 12;
    }
    input = Base(1 << 1);
    input.player_victim = false;
    if (Classify(input) != Decision::non_player_victim)
    {
        return 13;
    }
    input = Base(1 << 1);
    input.valid = false;
    if (Classify(input) != Decision::invalid)
    {
        return 14;
    }
    return 0;
}
