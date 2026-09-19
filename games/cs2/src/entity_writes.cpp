#include <keels2/cs2/entity_writes.h>
#include <keels2/cs2/native_bridge.h>
#include <keels2/platform/file_fingerprint.h>
#include <array>
#include <cstring>

namespace keels2::cs2
{
KeelResult ResolveEntityWrites(const platform::LoadedModule& module,
    const std::string& profile, void*& notify, std::string& error)
{
    notify = nullptr;
    error.clear();
#if defined(_WIN32)
    constexpr const char* supported = "cs2-25218825-win64-33042584-2212b672d2410a30";
    constexpr platform::FileFingerprint expected{33042584, 0x2212b672d2410a30ull};
    constexpr std::uintptr_t rva = 0x158590;
    constexpr std::array<unsigned char, 16> bytes{0x48,0x8b,0xc4,0x56,0x48,0x81,0xec,0x40,0x01,0x00,0x00,0x83,0x3a,0x00,0x48,0x8b};
#else
    constexpr const char* supported = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";
    constexpr platform::FileFingerprint expected{40575640, 0xb2ce91a0f330222aull};
    constexpr std::uintptr_t rva = 0xa3b5d0;
    constexpr std::array<unsigned char, 16> bytes{0x55,0x48,0x89,0xe5,0x53,0x48,0x89,0xfb,0x48,0x83,0xc7,0x38,0x48,0x83,0xec,0x08};
#endif
    if (profile != supported)
    {
        error = "entity writes are unavailable for this compatibility profile";
        return KEEL_RESULT_UNSUPPORTED;
    }
    platform::FileFingerprint fingerprint;
    if (!platform::FingerprintFile(module.path, fingerprint, error) ||
        fingerprint != expected)
    {
        error = "entity write server fingerprint is incompatible";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module.base);
    if (!base || rva > UINTPTR_MAX - base || bytes.size() - 1 > UINTPTR_MAX - base - rva)
    {
        error = "entity notification address is outside the module";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    auto* address = reinterpret_cast<void*>(base + rva);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (!platform::IsExecutableAddress(module, reinterpret_cast<void*>(base + rva + i)))
        {
            error = "entity notification code is not readable and executable";
            return KEEL_RESULT_INCOMPATIBLE;
        }
    if (std::memcmp(address, bytes.data(), bytes.size()))
    {
        error = "entity notification code changed";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    void** table{};
    if (platform::FindPrimaryVtable(module, "CBaseEntity", 30, table, error) != platform::ModuleLookup::found ||
        table[KEELS2_CS2_NETWORK_STATE_CHANGED_SLOT] != address)
    {
        error = "entity notification virtual method is incompatible";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    notify = address;
    return KEEL_RESULT_OK;
}
}
