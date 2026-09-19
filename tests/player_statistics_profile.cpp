#include <keels2/cs2/player_statistics.h>
#include <keels2/platform/file_fingerprint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
namespace {
#if defined(_WIN32)
constexpr const char* profile_name = "cs2-25218825-win64-33042584-2212b672d2410a30";
constexpr const char* other_profile = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";
constexpr std::size_t rva = 0x158590, notify_slot = 28;
constexpr std::array<unsigned char, 16> bytes{
    0x48, 0x8b, 0xc4, 0x56, 0x48, 0x81, 0xec, 0x40, 0x01, 0x00, 0x00, 0x83, 0x3a, 0x00, 0x48, 0x8b};

keels2::platform::FileFingerprint fingerprint{33042584, 0x2212b672d2410a30ull};
#else
constexpr const char* profile_name = "cs2-25218825-linuxsteamrt64-40575640-b2ce91a0f330222a";
constexpr const char* other_profile = "cs2-25218825-win64-33042584-2212b672d2410a30";
constexpr std::size_t rva = 0xa3b5d0, notify_slot = 29;
constexpr std::array<unsigned char, 16> bytes{
    0x55, 0x48, 0x89, 0xe5, 0x53, 0x48, 0x89, 0xfb, 0x48, 0x83, 0xc7, 0x38, 0x48, 0x83, 0xec, 0x08};

keels2::platform::FileFingerprint fingerprint{40575640, 0xb2ce91a0f330222aull};
#endif
bool readable = true;
keels2::platform::ModuleLookup lookup = keels2::platform::ModuleLookup::found;
std::array<void*, 30> table{}, controller{};
std::array<void*,3> money{}, tracking{};
std::string failed_class;
}

namespace keels2::platform {
bool FingerprintFile(const std::filesystem::path&, FileFingerprint& output, std::string&)
{
    output = fingerprint;
    return readable;
}

bool IsExecutableAddress(const LoadedModule& module, const void* pointer) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return std::any_of(module.ranges.begin(), module.ranges.end(), [&](const auto& range) {
        const auto start = reinterpret_cast<std::uintptr_t>(range.address);
        return range.readable && range.executable && address >= start && address - start < range.size;
    });
}

ModuleLookup
FindPrimaryVtable(const LoadedModule&, std::string_view name, std::size_t count, void**& output, std::string&)
{
    output = nullptr;

    if (name == failed_class)
        return ModuleLookup::ambiguous;

    if (lookup != ModuleLookup::found)
        return lookup;

    if (name == "CBaseEntity" && count == 30)
        output = table.data();

    if (name == "CCSPlayerController" && count == 30)
        output = controller.data();

    if (name == "CCSPlayerController_InGameMoneyServices" && count == 3)
        output = money.data();

    if (name == "CCSPlayerController_ActionTrackingServices" && count == 3)
        output = tracking.data();

    return output ? ModuleLookup::found : ModuleLookup::failed;
}
}

int main() {
    using namespace keels2;
    platform::LoadedModule module;
    KeelCs2PlayerStatisticsBindings bindings{};
    std::string error;
    std::vector<std::byte> image(rva + bytes.size());
    std::memcpy(image.data() + rva, bytes.data(), bytes.size());
    module.base = image.data();
    module.path = "fixture-server.so";
    module.image_size = image.size();
    module.ranges.push_back({image.data() + rva, bytes.size(), true, true});
    table[notify_slot] = image.data() + rva;
    controller[notify_slot] = table[notify_slot];

    const auto resolve = [&](KeelResult expected, const char* profile = profile_name)
    {
        bindings = {table.data(), table.data(), table.data(), image.data()};
        const auto result = cs2::ResolvePlayerStatistics(module,profile,bindings,error);
        return result == expected &&
               (result == KEEL_RESULT_OK
                    ? bindings.notify == image.data() + rva && bindings.controller_vtable == controller.data() &&
                          bindings.money_vtable == money.data() && bindings.tracking_vtable == tracking.data()
                    : !bindings.notify && !bindings.controller_vtable && !bindings.money_vtable &&
                          !bindings.tracking_vtable);
    };

    if (!resolve(KEEL_RESULT_UNSUPPORTED, "unknown") || !resolve(KEEL_RESULT_UNSUPPORTED, other_profile) ||
        !resolve(KEEL_RESULT_OK))
        return 1;

    ++fingerprint.fnv1a64;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 2;

    --fingerprint.fnv1a64;
    readable = false;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 3;

    readable = true;
    image[rva + 4] ^= std::byte{1};

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 4;

    image[rva + 4] ^= std::byte{1};
    module.ranges[0].readable = false;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 5;

    module.ranges[0].readable = true;
    module.ranges[0].executable = false;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 6;

    module.ranges[0].executable = true;
    --module.ranges[0].size;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 7;

    ++module.ranges[0].size;
    table[notify_slot] = image.data();

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 8;

    table[notify_slot] = image.data() + rva;
    controller[notify_slot] = table[notify_slot];
    lookup = platform::ModuleLookup::ambiguous;

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 9;

    lookup = platform::ModuleLookup::found;
    controller[notify_slot] = image.data();

    if (!resolve(KEEL_RESULT_INCOMPATIBLE))
        return 11;

    controller[notify_slot] = table[notify_slot];

    for (const char* name : {"CCSPlayerController",
                             "CCSPlayerController_InGameMoneyServices",
                             "CCSPlayerController_ActionTrackingServices"})
    {
        failed_class = name;

        if (!resolve(KEEL_RESULT_INCOMPATIBLE))
            return 12;
    }

    failed_class.clear();
    module.base = reinterpret_cast<void*>(UINTPTR_MAX - rva - 8);
    return resolve(KEEL_RESULT_INCOMPATIBLE) ? 0 : 10;
}
