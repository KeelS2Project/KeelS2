#include <keels2/cs2/entity_tools.h>
#include <keels2/platform/file_fingerprint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
namespace {
struct Entry { std::size_t rva; std::array<unsigned char,16> bytes; };
#if defined(_WIN32)
constexpr const char* profile = "cs2-25218825-win64-33042584-2212b672d2410a30";
constexpr unsigned slot = 163;
keels2::platform::FileFingerprint fingerprint{33042584,0x2212b672d2410a30ull};
constexpr std::array<Entry,3> entries{{
    {0xb3a8c0,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x4c,0x8b,0xc2,0x48,0x8b,0x0d,0x95}},
    {0xbe74f0,{0x48,0x85,0xc9,0x74,0x0f,0x48,0x8b,0xd1,0x48,0x8b,0x0d,0xb1,0x30,0x58,0x01,0xe9}},
    {0x3e5db0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}},
}};
#else
constexpr const char* profile = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";
constexpr unsigned slot = 162;
keels2::platform::FileFingerprint fingerprint{40575640,0xb2ce91a0f330222aull};
constexpr std::array<Entry,3> entries{{
    {0x1613a00,{0x55,0x48,0x89,0xe5,0x53,0x48,0x89,0xfb,0x48,0x83,0xec,0x08,0x48,0x8d,0x05,0x7d}},
    {0x16e39a0,{0x48,0x89,0xfe,0x48,0x85,0xff,0x74,0x18,0x48,0x8d,0x05,0x21,0x14,0x34,0x01,0x48}},
    {0xd79700,{0x8b,0x05,0x2a,0xff,0x9a,0x01,0x55,0x49,0x89,0xf9,0x49,0x89,0xf3,0x48,0x89,0xe5}},
}};
#endif
std::array<void*,slot+1> base_table{}, derived_table{};
bool readable = true;
keels2::platform::ModuleLookup lookup = keels2::platform::ModuleLookup::found;
}
namespace keels2::platform {
bool FingerprintFile(const std::filesystem::path&, FileFingerprint& output, std::string&) { output = fingerprint; return readable; }
bool IsExecutableAddress(const LoadedModule& module, const void* pointer) {
    const auto p = reinterpret_cast<std::uintptr_t>(pointer);
    return std::any_of(module.ranges.begin(),module.ranges.end(),[&](const auto& range) {
        const auto start = reinterpret_cast<std::uintptr_t>(range.address);
        return range.readable && range.executable && p >= start && p-start < range.size;
    });
}
ModuleLookup FindPrimaryVtable(const LoadedModule&, std::string_view name, std::size_t count, void**& output, std::string&) {
    output = nullptr;
    if (lookup != ModuleLookup::found) return lookup;
    if (count != slot+1) return ModuleLookup::failed;
    if (name == "CBaseEntity") output = base_table.data();
    else if (name == "CPhysicsProp") output = derived_table.data();
    return output ? ModuleLookup::found : ModuleLookup::failed;
}
}
int main() {
    using namespace keels2;
    const auto end = std::max_element(entries.begin(),entries.end(),[](const auto& a,const auto& b) { return a.rva < b.rva; })->rva+16;
    std::vector<std::byte> image(end+16);
    platform::LoadedModule module; module.base = image.data(); module.image_size = image.size(); module.path = "fixture-server";
    for (const auto& e : entries) {
        std::memcpy(image.data()+e.rva,e.bytes.data(),16); module.ranges.push_back({image.data()+e.rva,16,true,true});
    }
    module.ranges.push_back({image.data()+end,16,true,true});
    base_table[slot] = image.data()+entries[2].rva; derived_table[slot] = image.data()+end;
    KeelCs2EntityToolBindings bindings{}; std::string error;
    const auto resolve = [&](KeelResult expected,const char* name = profile) {
        bindings = {image.data(),image.data(),999,1};
        const auto result = cs2::ResolveEntityTools(module,name,bindings,error);
        return result == expected && (result == KEEL_RESULT_OK ? bindings.set_model == image.data()+entries[0].rva &&
            bindings.remove == image.data()+entries[1].rva && bindings.teleport_slot == slot && !bindings.reserved :
            !bindings.set_model && !bindings.remove && !bindings.teleport_slot && !bindings.reserved);
    };
    if (!resolve(KEEL_RESULT_UNSUPPORTED,"unknown") || !resolve(KEEL_RESULT_OK)) return 1;
    ++fingerprint.fnv1a64; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 2; --fingerprint.fnv1a64;
    readable = false; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 3; readable = true;
    for (unsigned i = 0; i < 3; ++i) {
        image[entries[i].rva+15] ^= std::byte{1}; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 4; image[entries[i].rva+15] ^= std::byte{1};
        module.ranges[i].readable = false; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 5; module.ranges[i].readable = true;
        module.ranges[i].executable = false; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 6; module.ranges[i].executable = true;
        --module.ranges[i].size; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 7; ++module.ranges[i].size;
    }
    base_table[slot] = derived_table[slot]; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 8; base_table[slot] = image.data()+entries[2].rva;
    lookup = platform::ModuleLookup::ambiguous; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 9; lookup = platform::ModuleLookup::found;
    if (!resolve(KEEL_RESULT_OK)) return 10;
    KeelCs2EntityToolClass target{};
    if (cs2::ResolveEntityToolClass(module,"CPhysicsProp",bindings,target,error) != KEEL_RESULT_OK ||
        target.vtable != derived_table.data() || target.teleport != derived_table[slot] || target.teleport == base_table[slot]) return 11;
    if (cs2::ResolveEntityToolClass(module,"missing",bindings,target,error) != KEEL_RESULT_INCOMPATIBLE || target.vtable || target.teleport) return 12;
    derived_table[slot] = image.data();
    if (cs2::ResolveEntityToolClass(module,"CPhysicsProp",bindings,target,error) != KEEL_RESULT_INCOMPATIBLE || target.teleport) return 13;
    ++bindings.teleport_slot;
    if (cs2::ResolveEntityToolClass(module,"CBaseEntity",bindings,target,error) != KEEL_RESULT_INVALID_ARGUMENT) return 14;
    module.base = reinterpret_cast<void*>(UINTPTR_MAX-entries[0].rva-8);
    return resolve(KEEL_RESULT_INCOMPATIBLE) ? 0 : 15;
}
