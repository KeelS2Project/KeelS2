#include <keels2/cs2/player_actions.h>
#include <keels2/platform/file_fingerprint.h>

#include <array>
#include <cstring>

namespace keels2::cs2
{
KeelResult ResolvePlayerActions(const platform::LoadedModule& module,
    const std::string& profile, KeelCs2PlayerActionBindings& bindings, std::string& error)
{
    bindings = {};
    error.clear();
    struct Entry { std::uintptr_t rva; std::array<unsigned char, 16> bytes; };
    std::array<Entry, 5> entries{};
    platform::FileFingerprint expected;
    std::uint32_t teleport_slot{};
    if (profile == "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a")
    {
        expected = {40575640, 0xb2ce91a0f330222aull};
        teleport_slot = 162;
        entries = {{
            {0x15b8c10, {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x49, 0x89, 0xf5, 0x41, 0x54, 0x49}},
            {0xadab90, {0xc6, 0x87, 0x31, 0x16, 0x00, 0x00, 0x01, 0x0f, 0xb6, 0xd2, 0x40, 0x0f, 0xb6, 0xf6, 0xe9, 0x3d}},
            {0x1add8e0, {0x48, 0x8d, 0x05, 0x99, 0xab, 0x91, 0x00, 0x55, 0x49, 0xba, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
            {0xd67c20, {0x55, 0x48, 0x89, 0xe5, 0x41, 0x55, 0x49, 0x89, 0xf5, 0x41, 0x54, 0x49, 0x89, 0xd4, 0x53, 0x48}},
            {0x1abf040, {0x55, 0x48, 0x89, 0xe5, 0x41, 0x55, 0x49, 0x89, 0xfd, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x08}}
        }};
    }
    else if (profile == "cs2-25218825-win64-33042584-2212b672d2410a30")
    {
        expected = {33042584, 0x2212b672d2410a30ull};
        teleport_slot = 163;
        entries = {{
            {0xaaf530, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x50, 0x49}},
            {0x1ca890, {0xc6, 0x81, 0x61, 0x13, 0x00, 0x00, 0x01, 0xe9, 0xd4, 0xb5, 0xab, 0x00, 0xcc, 0xcc, 0xcc, 0xcc}},
            {0xea5770, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48}},
            {0x3e5660, {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x48, 0x8d, 0x6c, 0x24, 0xc0, 0x48, 0x81, 0xec, 0x40}},
            {0xea6bc0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57}}
        }};
    }
    else
    {
        error = "player actions are unavailable for this compatibility profile";
        return KEEL_RESULT_UNSUPPORTED;
    }
    platform::FileFingerprint fingerprint;
    if (!platform::FingerprintFile(module.path, fingerprint, error))
        return KEEL_RESULT_INCOMPATIBLE;
    if (fingerprint != expected)
    {
        error = "player action server fingerprint changed";
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
            error = "player action address is outside the module";
            return KEEL_RESULT_INCOMPATIBLE;
        }
        void* address = reinterpret_cast<void*>(base + entry.rva);
        for (std::size_t offset = 0; offset < entry.bytes.size(); ++offset)
        {
            if (!platform::IsExecutableAddress(module, reinterpret_cast<void*>(base + entry.rva + offset)))
            {
                error = "player action code is not readable and executable";
                return KEEL_RESULT_INCOMPATIBLE;
            }
        }
        if (std::memcmp(address, entry.bytes.data(), entry.bytes.size()) != 0)
        {
            error = "player action code does not match the compatibility profile";
            return KEEL_RESULT_INCOMPATIBLE;
        }
        addresses[index] = address;
    }
    void** table{};
    if (platform::FindPrimaryVtable(module, "CCSPlayerPawn", 385, table, error) != platform::ModuleLookup::found)
        return KEEL_RESULT_INCOMPATIBLE;
    if (table[teleport_slot] != addresses[0] || table[384] != addresses[1])
    {
        error = "player action virtual methods do not match the compatibility profile";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    bindings = {teleport_slot, 384, addresses[2], addresses[3], addresses[4], 0x118};
    return KEEL_RESULT_OK;
}
}
