#include <keels2/cs2/player_actions.h>
#include <keels2/platform/file_fingerprint.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
constexpr const char* kProfile = "cs2-25218825-win64-33042584-2212b672d2410a30";
keels2::platform::FileFingerprint g_fingerprint{33042584, 0x2212b672d2410a30ull};
bool g_readable = true;
keels2::platform::ModuleLookup g_lookup = keels2::platform::ModuleLookup::found;
std::array<void*, 385> g_table{};
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
    if (name != "CCSPlayerPawn" || count != g_table.size())
        return ModuleLookup::failed;
    table = g_lookup == ModuleLookup::found ? g_table.data() : nullptr;
    if (!table) error = "fixture table unavailable";
    return g_lookup;
}
}

int main()
{
    using namespace keels2;
    struct Probe { std::size_t rva; std::array<unsigned char, 16> bytes; };
    constexpr std::array probes{
        Probe{0xaaf530, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x50,0x49}},
        Probe{0x1ca890, {0xc6,0x81,0x61,0x13,0x00,0x00,0x01,0xe9,0xd4,0xb5,0xab,0x00,0xcc,0xcc,0xcc,0xcc}},
        Probe{0xea5770, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}},
        Probe{0x3e5660, {0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x48,0x8d,0x6c,0x24,0xc0,0x48,0x81,0xec,0x40}},
        Probe{0xea6bc0, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}}
    };
    std::vector<std::byte> image(0xeb0000);
    platform::LoadedModule module;
    module.path = "fixture-server.dll";
    module.base = image.data();
    module.image_size = image.size();
    for (const auto& probe : probes)
    {
        std::memcpy(image.data() + probe.rva, probe.bytes.data(), probe.bytes.size());
        module.ranges.push_back({image.data() + probe.rva, probe.bytes.size(), true, true});
    }
    g_table[163] = image.data() + probes[0].rva;
    g_table[384] = image.data() + probes[1].rva;
    KeelCs2PlayerActionBindings bindings{};
    std::string error;
    const auto resolve = [&](KeelResult expected, const char* profile = kProfile) {
        bindings = {1,1,image.data(),image.data(),image.data(),1};
        const auto result = cs2::ResolvePlayerActions(module, profile, bindings, error);
        if (result != expected)
        {
            std::cerr << "player action profile result " << result << ", expected " << expected << '\n';
            return false;
        }
        return result == KEEL_RESULT_OK || (!bindings.teleport_slot && !bindings.suicide_slot &&
            !bindings.damage_construct && !bindings.damage_apply && !bindings.damage_destroy && !bindings.damage_info_size);
    };
    if (!resolve(KEEL_RESULT_UNSUPPORTED, "unreviewed-profile")) return 1;
    if (!resolve(KEEL_RESULT_OK) || bindings.teleport_slot != 163 || bindings.suicide_slot != 384 ||
        bindings.damage_info_size != 0x118 || bindings.damage_construct != image.data() + probes[2].rva ||
        bindings.damage_apply != image.data() + probes[3].rva || bindings.damage_destroy != image.data() + probes[4].rva)
        return 2;
    g_readable = false;
    if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 3;
    g_readable = true;
    ++g_fingerprint.fnv1a64;
    if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 4;
    --g_fingerprint.fnv1a64;
    for (std::size_t index = 0; index < probes.size(); ++index)
    {
        image[probes[index].rva + 8] ^= std::byte{1};
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 5;
        image[probes[index].rva + 8] ^= std::byte{1};
        module.ranges[index].executable = false;
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 6;
        module.ranges[index].executable = true;
        module.ranges[index].readable = false;
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 7;
        module.ranges[index].readable = true;
        --module.ranges[index].size;
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 8;
        ++module.ranges[index].size;
    }
    for (auto slot : {163u, 384u})
    {
        auto* saved = g_table[slot];
        g_table[slot] = image.data();
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 9;
        g_table[slot] = saved;
    }
    for (auto lookup : {platform::ModuleLookup::not_found, platform::ModuleLookup::ambiguous})
    {
        g_lookup = lookup;
        if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 10;
    }
    g_lookup = platform::ModuleLookup::found;
    auto& range = module.ranges.front();
    const auto saved_range = range;
    range.size = 8;
    module.ranges.push_back({saved_range.address + 9, 7, true, true});
    if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 11;
    module.ranges.pop_back();
    module.ranges.front() = saved_range;
    module.base = reinterpret_cast<void*>(UINTPTR_MAX - probes[0].rva - 8);
    if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 12;
    module.base = nullptr;
    if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 13;
    module.base = image.data();
    if (!resolve(KEEL_RESULT_OK) || !error.empty()) return 14;
    return 0;
}
