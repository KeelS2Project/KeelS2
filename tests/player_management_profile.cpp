#include <keels2/cs2/player_management.h>
#include <keels2/platform/file_fingerprint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>
namespace
{
keels2::platform::FileFingerprint g_fingerprint{40575640, 0xb2ce91a0f330222aull};
bool g_readable = true;
keels2::platform::ModuleLookup g_lookup = keels2::platform::ModuleLookup::found;
std::array<void*, 273> g_table{};
}
namespace keels2::platform
{
bool FingerprintFile(const std::filesystem::path&, FileFingerprint& output, std::string& error)
{
    output = g_fingerprint;
    if (!g_readable) error = "fixture file unavailable";
    return g_readable;
}

bool IsExecutableAddress(const LoadedModule& module, const void* pointer)
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return std::any_of(module.ranges.begin(), module.ranges.end(), [&](const auto& range) {
        const auto start = reinterpret_cast<std::uintptr_t>(range.address);
        return range.readable && range.executable && address >= start && address - start < range.size;
    });
}

ModuleLookup FindPrimaryVtable(const LoadedModule&, std::string_view name, std::size_t count,
    void**& table, std::string& error)
{
    if (name != "CCSPlayerController" || count != g_table.size())
        return ModuleLookup::failed;
    table = g_lookup == ModuleLookup::found ? g_table.data() : nullptr;
    if (!table) error = "fixture table unavailable";
    return g_lookup;
}
}

int main()
{
    using namespace keels2;
    platform::LoadedModule module;
    KeelCs2PlayerManagementBindings bindings{};
    std::string error;
#if defined(_WIN32)
    constexpr const char* profile = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";
    return cs2::ResolvePlayerManagement(module, profile, bindings, error) == KEEL_RESULT_UNSUPPORTED ? 0 : 1;
#else
    struct Probe { std::size_t rva; std::array<unsigned char, 16> bytes; };
    constexpr std::array probes{
        Probe{0x155ddf0, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
        Probe{0x155fb40, {0x55,0x48,0x89,0xe5,0x41,0x54,0x49,0x89,0xfc,0x89,0xf7,0x53,0x89,0xf3,0xe8,0x7d}},
        Probe{0x151f360, {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x89,0xfb}},
        Probe{0x1629840, {0x55,0x48,0x8d,0x87,0xc0,0x07,0x00,0x00,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41}}
    };
    std::vector<std::byte> image(0x1629860);
    module.path = "fixture-server.so"; module.base = image.data(); module.image_size = image.size();
    for (const auto& probe : probes)
    {
        std::memcpy(image.data() + probe.rva, probe.bytes.data(), probe.bytes.size());
        module.ranges.push_back({image.data() + probe.rva, probe.bytes.size(), true, true});
    }
    g_table[102] = image.data() + probes[0].rva;
    g_table[272] = image.data() + probes[2].rva;
    const auto resolve = [&](KeelResult expected, const char* selected = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a") {
        bindings = {g_table.data(), image.data(), image.data(), image.data(), image.data()};
        const auto result = cs2::ResolvePlayerManagement(module, selected, bindings, error);
        if (result != expected) { std::cerr << "unexpected binding result: " << result << '\n'; return false; }
        return result == KEEL_RESULT_OK || (!bindings.controller_vtable && !bindings.change_team &&
            !bindings.switch_team && !bindings.respawn && !bindings.set_pawn);
    };
    if (!resolve(KEEL_RESULT_UNSUPPORTED, "unreviewed")) return 1;
    if (!resolve(KEEL_RESULT_UNSUPPORTED, "cs2-25218825-win64-33042584-2212b672d2410a30")) return 2;
    if (!resolve(KEEL_RESULT_OK) || bindings.controller_vtable != g_table.data() ||
        bindings.change_team != g_table[102] || bindings.respawn != g_table[272] ||
        bindings.switch_team != image.data() + probes[1].rva || bindings.set_pawn != image.data() + probes[3].rva) return 3;
    g_readable = false;
    if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 4;
    g_readable = true; ++g_fingerprint.fnv1a64;
    if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 5;
    --g_fingerprint.fnv1a64;
    for (std::size_t i = 0; i < probes.size(); ++i)
    {
        image[probes[i].rva + 8] ^= std::byte{1};
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 6;
        image[probes[i].rva + 8] ^= std::byte{1};
        module.ranges[i].executable = false;
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 7;
        module.ranges[i].executable = true; module.ranges[i].readable = false;
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 8;
        module.ranges[i].readable = true; --module.ranges[i].size;
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 9;
        ++module.ranges[i].size;
    }
    for (auto slot : {102u,272u})
    {
        auto* saved = g_table[slot]; g_table[slot] = image.data();
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 10;
        g_table[slot] = saved;
    }
    for (auto status : {platform::ModuleLookup::not_found,platform::ModuleLookup::ambiguous})
    {
        g_lookup = status;
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 11;
    }
    g_lookup = platform::ModuleLookup::found;
    module.base = reinterpret_cast<void*>(UINTPTR_MAX - probes[0].rva - 8);
    return resolve(KEEL_RESULT_INCOMPATIBLE) ? 0 : 12;
#endif
}
