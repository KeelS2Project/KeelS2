#include "plugin_manifest_validation.h"
#include "service_name_validation.h"

#include <keels2/services.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace
{

std::uint64_t Next(std::uint64_t& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

template <std::size_t Size>
void Fill(std::array<char, Size>& output, std::uint64_t& state)
{
    const std::size_t length = static_cast<std::size_t>(Next(state) % Size);
    for (std::size_t index{}; index < length; ++index)
    {
        output[index] = static_cast<char>(Next(state) & 0x7fu);
    }
    output[length] = '\0';
    for (std::size_t index = length + 1; index < Size; ++index)
    {
        output[index] = static_cast<char>(0xa5);
    }
}

}

int main()
{
    const KeelPluginDependency valid_dependency{
        sizeof(KeelPluginDependency),
        KEELS2_PLUGIN_DEPENDENCY_AT_LEAST,
        "Fixture Provider",
        "1.2.3"
    };
    const KeelPluginManifest valid_manifest{
        sizeof(KeelPluginManifest),
        KEELS2_PLUGIN_MANIFEST_VERSION,
        1,
        0,
        &valid_dependency
    };
    std::vector<keels2::host::ValidatedPluginDependency> output;
    if (!keels2::host::ValidatePluginManifest(valid_manifest, output) ||
        output.size() != 1 || output[0].name != "Fixture Provider" ||
        output[0].version != "1.2.3")
    {
        return 1;
    }

    std::uint64_t state = 0x4b65656c53323039ull;
    std::array<std::array<char, 140>, 8> names{};
    std::array<std::array<char, 80>, 8> versions{};
    std::array<KeelPluginDependency, 8> dependencies{};
    for (std::size_t iteration{}; iteration < 50000; ++iteration)
    {
        for (std::size_t index{}; index < dependencies.size(); ++index)
        {
            Fill(names[index], state);
            Fill(versions[index], state);
            dependencies[index] = {
                (Next(state) & 3u) == 0 ? 0u :
                    static_cast<std::uint32_t>(sizeof(KeelPluginDependency)),
                static_cast<std::uint32_t>(Next(state) % 4u),
                (Next(state) & 7u) == 0 ? nullptr : names[index].data(),
                (Next(state) & 7u) == 0 ? nullptr : versions[index].data()
            };
        }
        std::uint32_t count = static_cast<std::uint32_t>(Next(state) % 10u);
        if (count == 9)
        {
            count = 65;
        }
        const KeelPluginManifest manifest{
            (Next(state) & 3u) == 0 ? 0u :
                static_cast<std::uint32_t>(sizeof(KeelPluginManifest)),
            static_cast<std::uint32_t>(Next(state) % 3u),
            count,
            static_cast<std::uint32_t>(Next(state) & 1u),
            (Next(state) & 7u) == 0 ? nullptr : dependencies.data()
        };
        const bool accepted = keels2::host::ValidatePluginManifest(manifest, output);
        if (accepted && output.size() != count)
        {
            return 2;
        }
        std::array<std::uint32_t, 3> parsed{};
        for (const auto& dependency : output)
        {
            if (!keels2::host::ValidPluginName(dependency.name.c_str()) ||
                !keels2::host::ParseSemanticVersion(dependency.version, parsed) ||
                (dependency.requirement != KEELS2_PLUGIN_DEPENDENCY_EXACT &&
                    dependency.requirement != KEELS2_PLUGIN_DEPENDENCY_AT_LEAST))
            {
                return 3;
            }
        }
    }
    std::array<char, 140> metadata{};
    std::array<std::uint32_t, 3> parsed{};
    for (std::size_t iteration{}; iteration < 50000; ++iteration)
    {
        Fill(metadata, state);
        static_cast<void>(keels2::host::ValidMetadataText(
            metadata.data(),
            static_cast<std::size_t>(Next(state) % 139u),
            (Next(state) & 1u) != 0));
        static_cast<void>(keels2::host::ValidPluginName(metadata.data()));
        static_cast<void>(keels2::host::ParseSemanticVersion(metadata.data(), parsed));
        std::string canonical;
        if (keels2::host::CanonicalServiceName(metadata.data(), canonical) &&
            (canonical.empty() || canonical.size() >= KEELS2_SERVICE_NAME_CAPACITY ||
                canonical.rfind("keels2.", 0) == 0 ||
                !std::all_of(
                    canonical.begin(),
                    canonical.end(),
                    [](unsigned char character) {
                        return std::isdigit(character) ||
                            (character >= 'a' && character <= 'z') || character == '.' ||
                            character == '_' || character == '-';
                    })))
        {
            return 4;
        }
    }
    return state == std::numeric_limits<std::uint64_t>::max() ? 5 : 0;
}
