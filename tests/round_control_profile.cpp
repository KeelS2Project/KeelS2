#include <keels2/cs2/round_control.h>
#include <keels2/platform/file_fingerprint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>
namespace
{
#if defined(_WIN32)
constexpr const char *profile = "cs2-25218825-win64-33042584-2212b672d2410a30",
                     *other_profile = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";

keels2::platform::FileFingerprint g_fingerprint{33042584, 0x2212b672d2410a30ull};
#else
constexpr const char *profile = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a",
                     *other_profile = "cs2-25218825-win64-33042584-2212b672d2410a30";

keels2::platform::FileFingerprint g_fingerprint{40575640, 0xb2ce91a0f330222aull};
#endif
bool g_readable = true;
keels2::platform::ModuleLookup g_lookup = keels2::platform::ModuleLookup::found;
std::array<void*, 3> g_table{}, g_proxy{};
}

namespace keels2::platform
{
bool FingerprintFile(const std::filesystem::path&, FileFingerprint& output, std::string& error)
{
    output = g_fingerprint;

    if (!g_readable)
        error = "fixture file unavailable";

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
    if ((name != "CCSGameRules" && name != "CCSGameRulesProxy") || count != 3)
        return ModuleLookup::failed;

    table = g_lookup == ModuleLookup::found ? (name == "CCSGameRules" ? g_table.data() : g_proxy.data()) : nullptr;

    if (!table)
        error = "fixture table unavailable";

    return g_lookup;
}
}

int main()
{
    using namespace keels2;
    platform::LoadedModule module;
    KeelCs2RoundBindings bindings{};
    std::string error;
#if defined(_WIN32)
    constexpr std::size_t rva = 0x93d800;
    constexpr std::array<unsigned char, 16> bytes{
        0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x48, 0x20, 0x48, 0x89, 0x48, 0x08, 0x55, 0x41, 0x54, 0x41, 0x56};
#else
    constexpr std::size_t rva = 0x13d6f10;
    constexpr std::array<unsigned char, 16> bytes{
        0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x41, 0x89, 0xf4, 0x53};
#endif
    std::vector<std::byte> image(rva + bytes.size());
    module.path = "fixture-server";
    module.base = image.data();
    module.image_size = image.size();
    std::memcpy(image.data() + rva,bytes.data(),bytes.size());
    module.ranges.push_back({image.data() + rva, bytes.size(), true, true});

    const auto resolve = [&](KeelResult expected, const char* selected = profile)
    {
        bindings = {g_table.data(), g_proxy.data(), image.data()};
        const auto result = cs2::ResolveRoundControl(module, selected, bindings, error);

        if (result != expected)
        {
            std::cerr << "unexpected binding result: " << result << '\n';
            return false;
        }

        return result == KEEL_RESULT_OK || (!bindings.rules_vtable && !bindings.proxy_vtable && !bindings.terminate);
    };

    if (!resolve(KEEL_RESULT_UNSUPPORTED, "unreviewed"))
        return 1;

    if (!resolve(KEEL_RESULT_UNSUPPORTED, other_profile))
        return 2;

    if (!resolve(KEEL_RESULT_OK) || bindings.rules_vtable != g_table.data() ||
        bindings.proxy_vtable != g_proxy.data() || bindings.terminate != image.data() + rva)
        return 3;

    g_readable = false;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 4;

    g_readable = true;
    ++g_fingerprint.fnv1a64;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 5;

    --g_fingerprint.fnv1a64;
    image[rva + 8] ^= std::byte{1};

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 6;

    image[rva+8] ^= std::byte{1};
    module.ranges[0].executable = false;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 7;

    module.ranges[0].executable = true;
    module.ranges[0].readable = false;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 8;

    module.ranges[0].readable = true;
    --module.ranges[0].size;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 9;

    ++module.ranges[0].size;

    for (auto status : {platform::ModuleLookup::not_found,platform::ModuleLookup::ambiguous}) {
        g_lookup = status;

        if (!resolve(KEEL_RESULT_INCOMPATIBLE))
            return 10;
    }

    g_lookup = platform::ModuleLookup::found;
    module.base = reinterpret_cast<void*>(UINTPTR_MAX - rva - 8);
    return resolve(KEEL_RESULT_INCOMPATIBLE) ? 0 : 11;
}
