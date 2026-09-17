#include <keels2/cs2/round_control.h>
#include <keels2/platform/file_fingerprint.h>
#include <array>
#include <cstring>

namespace keels2::cs2
{
KeelResult ResolveRoundControl(const platform::LoadedModule& module,
    const std::string& profile, KeelCs2RoundBindings& bindings, std::string& error)
{
    bindings = {};
    error.clear();
#if defined(_WIN32)
    static_cast<void>(module);
    static_cast<void>(profile);
    error = "round control has no reviewed Windows bindings";
    return KEEL_RESULT_UNSUPPORTED;
#else
    if (profile != "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a")
    {
        error = "round control is unavailable for this compatibility profile";
        return KEEL_RESULT_UNSUPPORTED;
    }
    platform::FileFingerprint fingerprint;
    if (!platform::FingerprintFile(module.path, fingerprint, error)) return KEEL_RESULT_INCOMPATIBLE;
    if (fingerprint != platform::FileFingerprint{40575640, 0xb2ce91a0f330222aull})
    {
        error = "round control server fingerprint changed";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    constexpr std::uintptr_t rva = 0x13d6f10;
    constexpr std::array<unsigned char,16> bytes{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x41,0x89,0xf4,0x53};
    const auto base = reinterpret_cast<std::uintptr_t>(module.base);
    if (!base || rva > UINTPTR_MAX - base || bytes.size()-1 > UINTPTR_MAX - base - rva)
    {
        error = "round control address is outside the module";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    auto* address = reinterpret_cast<void*>(base + rva);
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        if (!platform::IsExecutableAddress(module, reinterpret_cast<void*>(base + rva + i)))
        {
            error = "round control code is not readable and executable";
            return KEEL_RESULT_INCOMPATIBLE;
        }
    }
    if (std::memcmp(address,bytes.data(),bytes.size()))
    {
        error = "round control code does not match the compatibility profile";
        return KEEL_RESULT_INCOMPATIBLE;
    }
    void** rules{};
    void** proxy{};
    if (platform::FindPrimaryVtable(module,"CCSGameRules",3,rules,error) != platform::ModuleLookup::found ||
        platform::FindPrimaryVtable(module,"CCSGameRulesProxy",3,proxy,error) != platform::ModuleLookup::found)
        return KEEL_RESULT_INCOMPATIBLE;
    bindings = {rules,proxy,address};
    return KEEL_RESULT_OK;
#endif
}
}
