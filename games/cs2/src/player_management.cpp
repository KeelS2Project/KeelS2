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
#if defined(_WIN32)
    static_cast<void>(module);
    static_cast<void>(profile);
    error = "player management has no reviewed Windows bindings";
    return KEEL_RESULT_UNSUPPORTED;
#else
    // Statically inspected Ubuntu binary; runtime acceptance remains separate.
    if (profile != "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a")
    {
        error = "player management is unavailable for this compatibility profile";
        return KEEL_RESULT_UNSUPPORTED;
    }
    const platform::FileFingerprint expected{40575640, 0xb2ce91a0f330222aull};
    struct Entry { std::uintptr_t rva; std::array<unsigned char, 16> bytes; };
    constexpr std::array entries{
        Entry{0x155ddf0, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
        Entry{0x155fb40, {0x55,0x48,0x89,0xe5,0x41,0x54,0x49,0x89,0xfc,0x89,0xf7,0x53,0x89,0xf3,0xe8,0x7d}},
        Entry{0x151f360, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x89,0xfb}},
        Entry{0x1629840, {0x55,0x48,0x8d,0x87,0xc0,0x07,0x00,0x00,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41}}
    };
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
    if (platform::FindPrimaryVtable(module, "CCSPlayerController", 273, table, error) != platform::ModuleLookup::found)
        return KEEL_RESULT_INCOMPATIBLE;
    if (table[102] != addresses[0] || table[272] != addresses[2])
    {
        error = "player management virtual methods do not match the compatibility profile";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    bindings = {table, addresses[0], addresses[1], addresses[2], addresses[3]};
    return KEEL_RESULT_OK;
#endif
}
}
