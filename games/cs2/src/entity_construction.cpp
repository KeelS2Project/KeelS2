#include <keels2/cs2/entity_construction.h>
#include <keels2/platform/file_fingerprint.h>
#include <array>
#include <cstring>
namespace keels2::cs2 {
KeelResult ResolveEntityConstruction(const platform::LoadedModule& module, const std::string& profile,
    KeelCs2EntityConstructionBindings& bindings, std::string& error)
{
    bindings = {}; error.clear();
    struct Entry { std::uintptr_t rva; std::array<unsigned char,16> bytes; };
#if defined(_WIN32)
    constexpr const char* supported = "cs2-25218825-win64-33042584-2212b672d2410a30";
    constexpr platform::FileFingerprint expected{33042584,0x2212b672d2410a30ull};
    constexpr std::array<Entry,3> entries{{
        {0xba7f90, {0x48,0x83,0xec,0x48,0xc6,0x44,0x24,0x30,0x00,0x4c,0x8b,0xc1,0x48,0x8b,0x0d,0x0d}},
        {0xc57b40, {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xda,0x48,0x8b,0xf9}},
        {0xbe74f0, {0x48,0x85,0xc9,0x74,0x0f,0x48,0x8b,0xd1,0x48,0x8b,0x0d,0xb1,0x30,0x58,0x01,0xe9}},
    }};
#else
    constexpr const char* supported = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";
    constexpr platform::FileFingerprint expected{40575640,0xb2ce91a0f330222aull};
    constexpr std::array<Entry,3> entries{{
        {0x16e3680, {0x48,0x8d,0x05,0x49,0x17,0x34,0x01,0x55,0x48,0x89,0xfa,0x41,0x89,0xf0,0x48,0x89}},
        {0x17c0040, {0x48,0x85,0xff,0x74,0x4b,0x55,0x48,0x89,0xe5,0x41,0x55,0x41,0x54,0x49,0x89,0xfc}},
        {0x16e39a0, {0x48,0x89,0xfe,0x48,0x85,0xff,0x74,0x18,0x48,0x8d,0x05,0x21,0x14,0x34,0x01,0x48}},
    }};
#endif
    if (profile != supported) { error = "entity construction are unavailable for this compatibility profile"; return KEEL_RESULT_UNSUPPORTED; }
    platform::FileFingerprint fingerprint;
    if (!platform::FingerprintFile(module.path,fingerprint,error) || fingerprint != expected)
    { error = "entity construction server fingerprint changed"; return KEEL_RESULT_INCOMPATIBLE; }
    const auto base = reinterpret_cast<std::uintptr_t>(module.base);
    std::array<void*,3> functions{};
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (!base || entry.rva > UINTPTR_MAX-base || entry.bytes.size()-1 > UINTPTR_MAX-base-entry.rva)
        { error = "entity construction address is outside the module"; return KEEL_RESULT_INCOMPATIBLE; }
        for (std::size_t j = 0; j < entry.bytes.size(); ++j)
            if (!platform::IsExecutableAddress(module,reinterpret_cast<void*>(base+entry.rva+j)))
            { error = "entity construction code is not readable and executable"; return KEEL_RESULT_INCOMPATIBLE; }
        functions[i] = reinterpret_cast<void*>(base+entry.rva);
        if (std::memcmp(functions[i],entry.bytes.data(),entry.bytes.size()))
        { error = "entity construction code changed"; return KEEL_RESULT_INCOMPATIBLE; }
    }
    bindings = {functions[0],functions[1],functions[2]};
    return KEEL_RESULT_OK;
}
}
