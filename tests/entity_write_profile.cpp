#include <keels2/cs2/entity_writes.h>
#include <keels2/platform/file_fingerprint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
namespace {
keels2::platform::FileFingerprint fingerprint{40575640, 0xb2ce91a0f330222aull};
bool readable = true;
keels2::platform::ModuleLookup lookup = keels2::platform::ModuleLookup::found;
std::array<void*, 30> table{};
}
namespace keels2::platform {
bool FingerprintFile(const std::filesystem::path&, FileFingerprint& output, std::string&) { output = fingerprint; return readable; }
bool IsExecutableAddress(const LoadedModule& module, const void* pointer) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return std::any_of(module.ranges.begin(), module.ranges.end(), [&](const auto& range) {
        const auto start = reinterpret_cast<std::uintptr_t>(range.address);
        return range.readable && range.executable && address >= start && address - start < range.size;
    });
}
ModuleLookup FindPrimaryVtable(const LoadedModule&, std::string_view name, std::size_t count, void**& output, std::string&) {
    if (name != "CBaseEntity" || count != table.size()) return ModuleLookup::failed;
    output = lookup == ModuleLookup::found ? table.data() : nullptr;
    return lookup;
}
}
int main() {
    using namespace keels2;
    platform::LoadedModule module;
    void* notify{};
    std::string error;
#if defined(_WIN32)
    return cs2::ResolveEntityWrites(module, "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a", notify, error) == KEEL_RESULT_UNSUPPORTED ? 0 : 1;
#else
    constexpr std::size_t rva = 0xa3b5d0;
    constexpr std::array<unsigned char,16> bytes{0x55,0x48,0x89,0xe5,0x53,0x48,0x89,0xfb,0x48,0x83,0xc7,0x38,0x48,0x83,0xec,0x08};
    std::vector<std::byte> image(rva + bytes.size());
    std::memcpy(image.data() + rva, bytes.data(), bytes.size());
    module.base = image.data(); module.path = "fixture-server.so"; module.image_size = image.size();
    module.ranges.push_back({image.data() + rva,bytes.size(),true,true}); table[29] = image.data() + rva;
    const auto resolve = [&](KeelResult expected, const char* profile = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a") {
        notify = image.data();
        const auto result = cs2::ResolveEntityWrites(module,profile,notify,error);
        return result == expected && (result == KEEL_RESULT_OK ? notify == image.data() + rva : !notify);
    };
    if (!resolve(KEEL_RESULT_UNSUPPORTED,"unknown") || !resolve(KEEL_RESULT_OK)) return 1;
    ++fingerprint.fnv1a64; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 2; --fingerprint.fnv1a64;
    readable = false; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 3; readable = true;
    image[rva + 4] ^= std::byte{1}; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 4; image[rva + 4] ^= std::byte{1};
    module.ranges[0].readable = false; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 5; module.ranges[0].readable = true;
    module.ranges[0].executable = false; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 6; module.ranges[0].executable = true;
    --module.ranges[0].size; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 7; ++module.ranges[0].size;
    table[29] = image.data(); if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 8; table[29] = image.data() + rva;
    lookup = platform::ModuleLookup::ambiguous; if (!resolve(KEEL_RESULT_INCOMPATIBLE)) return 9; lookup = platform::ModuleLookup::found;
    module.base = reinterpret_cast<void*>(UINTPTR_MAX - rva - 8);
    return resolve(KEEL_RESULT_INCOMPATIBLE) ? 0 : 10;
#endif
}
