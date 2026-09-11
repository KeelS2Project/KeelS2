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
    if (profile != "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a")
        return KEEL_RESULT_UNSUPPORTED;
    platform::FileFingerprint fingerprint;
    if (!platform::FingerprintFile(module.path, fingerprint, error) ||
        fingerprint != platform::FileFingerprint{40575640, 0xb2ce91a0f330222aull})
        return KEEL_RESULT_INCOMPATIBLE;
    struct Entry { std::uintptr_t rva; std::array<unsigned char, 16> bytes; };
    constexpr std::array entries{
        Entry{0x15b8c10, {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x49, 0x89, 0xf5, 0x41, 0x54, 0x49}},
        Entry{0xadab90, {0xc6, 0x87, 0x31, 0x16, 0x00, 0x00, 0x01, 0x0f, 0xb6, 0xd2, 0x40, 0x0f, 0xb6, 0xf6, 0xe9, 0x3d}},
        Entry{0x1add8e0, {0x48, 0x8d, 0x05, 0x99, 0xab, 0x91, 0x00, 0x55, 0x49, 0xba, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
        Entry{0xd67c20, {0x55, 0x48, 0x89, 0xe5, 0x41, 0x55, 0x49, 0x89, 0xf5, 0x41, 0x54, 0x49, 0x89, 0xd4, 0x53, 0x48}},
        Entry{0x1abf040, {0x55, 0x48, 0x89, 0xe5, 0x41, 0x55, 0x49, 0x89, 0xfd, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x08}}
    };
    std::array<void*, entries.size()> addresses{};
    const auto base = reinterpret_cast<std::uintptr_t>(module.base);
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& entry = entries[index];
        if (entry.rva > UINTPTR_MAX - base)
            return KEEL_RESULT_INCOMPATIBLE;
        void* address = reinterpret_cast<void*>(base + entry.rva);
        if (!platform::IsExecutableAddress(module, address) ||
            !platform::IsExecutableAddress(module, reinterpret_cast<void*>(base + entry.rva + entry.bytes.size() - 1)) ||
            std::memcmp(address, entry.bytes.data(), entry.bytes.size()) != 0)
            return KEEL_RESULT_INCOMPATIBLE;
        addresses[index] = address;
    }
    void** table{};
    if (platform::FindPrimaryVtable(module, "CCSPlayerPawn", 385, table, error) != platform::ModuleLookup::found ||
        table[162] != addresses[0] || table[384] != addresses[1])
        return KEEL_RESULT_INCOMPATIBLE;
    bindings = {162, 384, addresses[2], addresses[3], addresses[4], 0x118};
    return KEEL_RESULT_OK;
}
}
