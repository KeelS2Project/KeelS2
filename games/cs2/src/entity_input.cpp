#include <keels2/cs2/entity_input.h>
#include <keels2/platform/file_fingerprint.h>
#include <array>
#include <cstring>
namespace keels2::cs2 {
KeelResult ResolveEntityInput(const platform::LoadedModule& module, const std::string& profile,
    KeelCs2EntityInputBindings& bindings, std::string& error)
{
    bindings = {};
    error.clear();

    struct Entry
    {
        std::uintptr_t rva;
        std::array<unsigned char, 16> bytes;
    };
#if defined(_WIN32)
    constexpr const char* supported = "cs2-25218825-win64-33042584-2212b672d2410a30";
    constexpr platform::FileFingerprint expected{33042584,0x2212b672d2410a30ull};
    constexpr std::array<Entry,2> entries{{
        {0x126bad0, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x50,0x49}},
        {0x1247710, {0x48,0x89,0x5c,0x24,0x18,0x4c,0x89,0x4c,0x24,0x20,0x48,0x89,0x4c,0x24,0x08,0x55}},
    }};
#else
    constexpr const char* supported = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";
    constexpr platform::FileFingerprint expected{40575640,0xb2ce91a0f330222aull};
    constexpr std::array<Entry,2> entries{{
        {0x213fc60, {0x55,0x48,0x89,0xe5,0x41,0x56,0x49,0x89,0xfe,0x41,0x55,0x48,0x8d,0x7d,0xd0,0x4d}},
        {0x2161d30, {0x55,0x48,0x89,0xe5,0x41,0x55,0x49,0x89,0xcd,0x41,0x54,0x49,0x89,0xfc,0x53,0xbb}},
    }};
#endif
    if (profile != supported)
    {
        error = "entity inputs are unavailable for this compatibility profile";
        return KEEL_RESULT_UNSUPPORTED;
    }

    platform::FileFingerprint fingerprint;

    if (!platform::FingerprintFile(module.path,fingerprint,error) || fingerprint != expected)
    {
        error = "entity input server fingerprint changed";
        return KEEL_RESULT_INCOMPATIBLE;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(module.base);
    std::array<void*,2> functions{};

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];

        if (!base || entry.rva > UINTPTR_MAX-base || entry.bytes.size()-1 > UINTPTR_MAX-base-entry.rva)
        {
            error = "entity input address is outside the module";
            return KEEL_RESULT_INCOMPATIBLE;
        }

        for (std::size_t j = 0; j < entry.bytes.size(); ++j)
            if (!platform::IsExecutableAddress(module,reinterpret_cast<void*>(base+entry.rva+j)))
            {
                error = "entity input code is not readable and executable";
                return KEEL_RESULT_INCOMPATIBLE;
            }

        functions[i] = reinterpret_cast<void*>(base+entry.rva);

        if (std::memcmp(functions[i],entry.bytes.data(),entry.bytes.size()))
        {
            error = "entity input code changed";
            return KEEL_RESULT_INCOMPATIBLE;
        }
    }

    bindings = {functions[0],functions[1]};
    return KEEL_RESULT_OK;
}
}
