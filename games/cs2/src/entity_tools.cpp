#include <keels2/cs2/entity_tools.h>
#include <keels2/platform/file_fingerprint.h>
#include <array>
#include <cstring>
namespace keels2::cs2 {
KeelResult ResolveEntityTools(const platform::LoadedModule& module, const std::string& profile,
    KeelCs2EntityToolBindings& bindings, std::string& error)
{
    bindings = {}; error.clear();
    struct Entry { std::uintptr_t rva; std::array<unsigned char,16> bytes; };
#if defined(_WIN32)
    constexpr const char* supported = "cs2-25218825-win64-33042584-2212b672d2410a30";
    constexpr platform::FileFingerprint expected{33042584,0x2212b672d2410a30ull};
    constexpr std::uint32_t slot = 163;
    constexpr std::array<Entry,3> entries{{
        {0xb3a8c0, {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x4c,0x8b,0xc2,0x48,0x8b,0x0d,0x95}},
        {0xbe74f0, {0x48,0x85,0xc9,0x74,0x0f,0x48,0x8b,0xd1,0x48,0x8b,0x0d,0xb1,0x30,0x58,0x01,0xe9}},
        {0x3e5db0, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}},
    }};
#else
    constexpr const char* supported = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";
    constexpr platform::FileFingerprint expected{40575640,0xb2ce91a0f330222aull};
    constexpr std::uint32_t slot = 162;
    constexpr std::array<Entry,3> entries{{
        {0x1613a00, {0x55,0x48,0x89,0xe5,0x53,0x48,0x89,0xfb,0x48,0x83,0xec,0x08,0x48,0x8d,0x05,0x7d}},
        {0x16e39a0, {0x48,0x89,0xfe,0x48,0x85,0xff,0x74,0x18,0x48,0x8d,0x05,0x21,0x14,0x34,0x01,0x48}},
        {0xd79700, {0x8b,0x05,0x2a,0xff,0x9a,0x01,0x55,0x49,0x89,0xf9,0x49,0x89,0xf3,0x48,0x89,0xe5}},
    }};
#endif
    if (profile != supported) { error = "entity tools are unavailable for this compatibility profile"; return KEEL_RESULT_UNSUPPORTED; }
    platform::FileFingerprint fingerprint;
    if (!platform::FingerprintFile(module.path,fingerprint,error) || fingerprint != expected)
    { error = "entity tools server fingerprint changed"; return KEEL_RESULT_INCOMPATIBLE; }
    const auto base = reinterpret_cast<std::uintptr_t>(module.base);
    std::array<void*,3> functions{};
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (!base || entry.rva > UINTPTR_MAX-base || entry.bytes.size()-1 > UINTPTR_MAX-base-entry.rva)
        { error = "entity tool address is outside the module"; return KEEL_RESULT_INCOMPATIBLE; }
        for (std::size_t j = 0; j < entry.bytes.size(); ++j)
            if (!platform::IsExecutableAddress(module,reinterpret_cast<void*>(base+entry.rva+j)))
            { error = "entity tool code is not readable and executable"; return KEEL_RESULT_INCOMPATIBLE; }
        functions[i] = reinterpret_cast<void*>(base+entry.rva);
        if (std::memcmp(functions[i],entry.bytes.data(),entry.bytes.size()))
        { error = "entity tool code changed"; return KEEL_RESULT_INCOMPATIBLE; }
    }
    void** table{};
    if (platform::FindPrimaryVtable(module,"CBaseEntity",slot+1,table,error) != platform::ModuleLookup::found || table[slot] != functions[2])
    { error = "entity teleport virtual method is incompatible"; return KEEL_RESULT_INCOMPATIBLE; }
    bindings = {functions[0],functions[1],slot,0};
    return KEEL_RESULT_OK;
}
KeelResult ResolveEntityToolClass(const platform::LoadedModule& module, const char* name,
    const KeelCs2EntityToolBindings& bindings, KeelCs2EntityToolClass& target, std::string& error)
{
    target = {}; error.clear();
#if defined(_WIN32)
    constexpr std::uint32_t slot = 163;
#else
    constexpr std::uint32_t slot = 162;
#endif
    if (!name || !*name || bindings.teleport_slot != slot || bindings.reserved || !bindings.set_model || !bindings.remove)
        return KEEL_RESULT_INVALID_ARGUMENT;
    void** table{};
    if (platform::FindPrimaryVtable(module,name,slot+1,table,error) != platform::ModuleLookup::found ||
        !platform::IsExecutableAddress(module,table[slot]))
    { error = "entity class teleport method is incompatible"; return KEEL_RESULT_INCOMPATIBLE; }
    target = {table,table[slot]};
    return KEEL_RESULT_OK;
}
}
