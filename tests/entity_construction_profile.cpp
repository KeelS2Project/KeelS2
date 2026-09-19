#include <keels2/cs2/entity_construction.h>
#include <keels2/platform/file_fingerprint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
namespace {
struct Entry
{
    std::uintptr_t rva;
    std::array<unsigned char, 16> bytes;
};
#if defined(_WIN32)
    constexpr const char* profile = "cs2-25218825-win64-33042584-2212b672d2410a30";

keels2::platform::FileFingerprint fingerprint{33042584,0x2212b672d2410a30ull};
    constexpr std::array<Entry,3> entries{{
        {0xba7f90, {0x48,0x83,0xec,0x48,0xc6,0x44,0x24,0x30,0x00,0x4c,0x8b,0xc1,0x48,0x8b,0x0d,0x0d}},
        {0xc57b40, {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xda,0x48,0x8b,0xf9}},
        {0xbe74f0, {0x48,0x85,0xc9,0x74,0x0f,0x48,0x8b,0xd1,0x48,0x8b,0x0d,0xb1,0x30,0x58,0x01,0xe9}},
    }};
#else
    constexpr const char* profile = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";

keels2::platform::FileFingerprint fingerprint{40575640,0xb2ce91a0f330222aull};
    constexpr std::array<Entry,3> entries{{
        {0x16e3680, {0x48,0x8d,0x05,0x49,0x17,0x34,0x01,0x55,0x48,0x89,0xfa,0x41,0x89,0xf0,0x48,0x89}},
        {0x17c0040, {0x48,0x85,0xff,0x74,0x4b,0x55,0x48,0x89,0xe5,0x41,0x55,0x41,0x54,0x49,0x89,0xfc}},
        {0x16e39a0, {0x48,0x89,0xfe,0x48,0x85,0xff,0x74,0x18,0x48,0x8d,0x05,0x21,0x14,0x34,0x01,0x48}},
    }};
#endif
bool readable = true;
}

namespace keels2::platform {
bool FingerprintFile(const std::filesystem::path&, FileFingerprint& output, std::string&)
{
    output = fingerprint;
    return readable;
}

bool IsExecutableAddress(const LoadedModule& module, const void* pointer) {
    const auto p = reinterpret_cast<std::uintptr_t>(pointer);
    return std::any_of(module.ranges.begin(),module.ranges.end(),[&](const auto& range) {
        const auto start = reinterpret_cast<std::uintptr_t>(range.address);
        return range.readable && range.executable && p >= start && p-start < range.size;
    });
}
}

int main() {
    using namespace keels2;
    const auto end = std::max_element(entries.begin(),
                                      entries.end(),
                                      [](const auto& a, const auto& b)
                                      {
                                          return a.rva < b.rva;
                                      })
                         ->rva +
                     16;

    std::vector<std::byte> image(end);
    platform::LoadedModule module;
    module.base = image.data();
    module.image_size = image.size();
    module.path = "fixture-server";

    for (const auto& e : entries) {
        std::memcpy(image.data() + e.rva, e.bytes.data(), 16);
        module.ranges.push_back({image.data() + e.rva, 16, true, true});
    }

    KeelCs2EntityConstructionBindings bindings{};
    std::string error;

    const auto resolve = [&](KeelResult expected, const char* name = profile)
    {
        bindings = {image.data(), image.data(), image.data()};
        const auto result = cs2::ResolveEntityConstruction(module,name,bindings,error);
        return result == expected && (result == KEEL_RESULT_OK ? bindings.create == image.data()+entries[0].rva &&
            bindings.spawn == image.data()+entries[1].rva && bindings.remove == image.data()+entries[2].rva :
            !bindings.create && !bindings.spawn && !bindings.remove);
    };

    if (!resolve(KEEL_RESULT_UNSUPPORTED, "unknown") || !resolve(KEEL_RESULT_OK))
        return 1;

    ++fingerprint.fnv1a64;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 2;

    --fingerprint.fnv1a64;
    ++fingerprint.size;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 3;

    --fingerprint.size;
    readable = false;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 4;

    readable = true;

    for (unsigned i = 0; i < entries.size(); ++i) {
        for (unsigned byte = 0; byte < 16; ++byte) {
            image[entries[i].rva + byte] ^= std::byte{1};

            if (!resolve(KEEL_RESULT_INCOMPATIBLE))
                return 5;

            image[entries[i].rva+byte] ^= std::byte{1};
        }

        module.ranges[i].readable = false;

        if (!resolve(KEEL_RESULT_INCOMPATIBLE))
            return 6;

        module.ranges[i].readable = true;
        module.ranges[i].executable = false;

        if (!resolve(KEEL_RESULT_INCOMPATIBLE))
            return 7;

        module.ranges[i].executable = true;
        --module.ranges[i].size;

        if (!resolve(KEEL_RESULT_INCOMPATIBLE))
            return 8;

        ++module.ranges[i].size;
    }

    if (!resolve(KEEL_RESULT_OK))
        return 9;

    module.base = nullptr;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 10;

    module.base = reinterpret_cast<void*>(UINTPTR_MAX-entries[0].rva-8);
    return resolve(KEEL_RESULT_INCOMPATIBLE) ? 0 : 11;
}
