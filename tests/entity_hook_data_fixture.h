#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hook_data_fixture {
inline constexpr std::array<int,8> offsets{80,44,88,56,68,72,8,104};

struct alignas(8) DamageRecord
{
    std::array<std::byte, 0x118> bytes;
};
inline DamageRecord MakeDamage() {
    DamageRecord value;
    value.bytes.fill(std::byte{0x5a});
    const float damage = 42.5f, force[]{1,2,3}, position[]{4,5,6};
    const std::uint32_t type = 0x80000040, inflictor = 0x6007, attacker = UINT32_MAX, ability = 0x7008;
    const std::int32_t custom = -7;
    const void* fields[]{&damage,&type,&custom,&inflictor,&attacker,&ability,force,position};

    for (unsigned i = 0; i < 8; ++i)
        std::memcpy(value.bytes.data() + offsets[i], fields[i], i >= 6 ? 12 : 4);

    return value;
}
}
