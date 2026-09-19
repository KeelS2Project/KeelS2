#include <keels2/cs2/player_management.h>
#include <keels2/platform/file_fingerprint.h>

#include <array>
#include <cstring>

namespace keels2::cs2
{
KeelResult ResolvePlayerManagement(const platform::LoadedModule& module,
    const std::string& profile, KeelCs2PlayerManagementBindings& bindings, std::string& error)
{
    bindings = {};
    error.clear();
    struct Entry { std::uintptr_t rva; std::array<unsigned char, 16> bytes; };
#if defined(_WIN32)
    constexpr const char* supported = "cs2-25218825-win64-33042584-2212b672d2410a30";
    constexpr platform::FileFingerprint expected{33042584, 0x2212b672d2410a30ull};
    constexpr std::size_t change_slot = 103, respawn_slot = 270;
    constexpr std::array entries{
        Entry{0xa3bfe0, {0x48,0x8b,0xc4,0x53,0x55,0x57,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x0f,0xb6,0xa9}},
        Entry{0xa668a0, {0x40,0x53,0x57,0x48,0x81,0xec,0x88,0x00,0x00,0x00,0x48,0x8b,0xd9,0x8b,0xfa,0x8b}},
        Entry{0xa64cb0, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x30,0xf3}},
        Entry{0xb3afc0, {0x44,0x88,0x4c,0x24,0x20,0x53,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec}}
    };
#else
    constexpr const char* supported = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";
    constexpr platform::FileFingerprint expected{40575640, 0xb2ce91a0f330222aull};
    constexpr std::size_t change_slot = 102, respawn_slot = 272;
    constexpr std::array entries{
        Entry{0x155ddf0, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
        Entry{0x155fb40, {0x55,0x48,0x89,0xe5,0x41,0x54,0x49,0x89,0xfc,0x89,0xf7,0x53,0x89,0xf3,0xe8,0x7d}},
        Entry{0x151f360, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x89,0xfb}},
        Entry{0x1629840, {0x55,0x48,0x8d,0x87,0xc0,0x07,0x00,0x00,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41}}
    };
#endif
    if (profile != supported)
    {
        error = "player management is unavailable for this compatibility profile";
        return KEEL_RESULT_UNSUPPORTED;
    }
    platform::FileFingerprint fingerprint;
    if (!platform::FingerprintFile(module.path, fingerprint, error))
        return KEEL_RESULT_INCOMPATIBLE;
    if (fingerprint != expected)
    {
        error = "player management server fingerprint changed";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    std::array<void*, entries.size()> addresses{};
    const auto base = reinterpret_cast<std::uintptr_t>(module.base);
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        if (!base || entry.rva > UINTPTR_MAX - base ||
            entry.bytes.size() - 1 > UINTPTR_MAX - base - entry.rva)
        {
            error = "player management address is outside the module";
            return KEEL_RESULT_INCOMPATIBLE;
        }
        void* address = reinterpret_cast<void*>(base + entry.rva);
        for (std::size_t offset = 0; offset < entry.bytes.size(); ++offset)
        {
            if (!platform::IsExecutableAddress(module, reinterpret_cast<void*>(base + entry.rva + offset)))
            {
                error = "player management code is not readable and executable";
                return KEEL_RESULT_INCOMPATIBLE;
            }
        }
        if (std::memcmp(address, entry.bytes.data(), entry.bytes.size()) != 0)
        {
            error = "player management code does not match the compatibility profile";
            return KEEL_RESULT_INCOMPATIBLE;
        }
        addresses[index] = address;
    }
    void** table{};
    if (platform::FindPrimaryVtable(module, "CCSPlayerController", respawn_slot + 1, table, error) != platform::ModuleLookup::found)
        return KEEL_RESULT_INCOMPATIBLE;
    if (table[change_slot] != addresses[0] || table[respawn_slot] != addresses[2])
    {
        error = "player management virtual methods do not match the compatibility profile";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    bindings = {table, addresses[0], addresses[1], addresses[2], addresses[3]};
    return KEEL_RESULT_OK;
}
}
