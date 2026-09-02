#include "plugin_manifest_validation.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <system_error>

namespace keels2::host
{

namespace
{

bool EqualInsensitive(std::string_view left, std::string_view right) noexcept
{
    return left.size() == right.size() &&
        std::equal(
            left.begin(),
            left.end(),
            right.begin(),
            [](unsigned char first, unsigned char second) {
                return std::tolower(first) == std::tolower(second);
            });
}

}

bool ValidMetadataText(const char* text, std::size_t maximum, bool allow_empty) noexcept
{
    if (!text)
    {
        return allow_empty;
    }
    std::size_t length{};
    while (length <= maximum && text[length])
    {
        if (std::iscntrl(static_cast<unsigned char>(text[length])) != 0)
        {
            return false;
        }
        ++length;
    }
    if (length == 0)
    {
        return allow_empty;
    }
    return length <= maximum &&
        std::isspace(static_cast<unsigned char>(text[0])) == 0 &&
        std::isspace(static_cast<unsigned char>(text[length - 1])) == 0;
}

bool ValidPluginName(const char* name) noexcept
{
    if (!ValidMetadataText(name, 127, false))
    {
        return false;
    }
    const std::string_view value(name);
    return !std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

bool ParseSemanticVersion(
    std::string_view version,
    std::array<std::uint32_t, 3>& output) noexcept
{
    output = {};
    std::size_t begin{};
    for (std::size_t component{}; component < output.size(); ++component)
    {
        const std::size_t end = component + 1 == output.size()
            ? version.size()
            : version.find('.', begin);
        if (end == std::string_view::npos || end == begin)
        {
            return false;
        }
        const char* first = version.data() + begin;
        const char* last = version.data() + end;
        const auto result = std::from_chars(first, last, output[component]);
        if (result.ec != std::errc{} || result.ptr != last)
        {
            return false;
        }
        if (component + 1 != output.size())
        {
            begin = end + 1;
        }
    }
    return begin <= version.size() &&
        version.find('.', begin) == std::string_view::npos;
}

bool ValidatePluginManifest(
    const KeelPluginManifest& manifest,
    std::vector<ValidatedPluginDependency>& dependencies) noexcept
{
    dependencies.clear();
    if (manifest.size != sizeof(KeelPluginManifest) ||
        manifest.manifest_version != KEELS2_PLUGIN_MANIFEST_VERSION ||
        manifest.reserved != 0 || manifest.dependency_count > 64 ||
        (manifest.dependency_count != 0 && !manifest.dependencies))
    {
        return false;
    }
    try
    {
        dependencies.reserve(manifest.dependency_count);
        for (std::uint32_t index{}; index < manifest.dependency_count; ++index)
        {
            const KeelPluginDependency& dependency = manifest.dependencies[index];
            std::array<std::uint32_t, 3> parsed{};
            if (dependency.size != sizeof(KeelPluginDependency) ||
                (dependency.requirement != KEELS2_PLUGIN_DEPENDENCY_EXACT &&
                    dependency.requirement != KEELS2_PLUGIN_DEPENDENCY_AT_LEAST) ||
                !ValidPluginName(dependency.name) ||
                !ValidMetadataText(dependency.version, 63, false) ||
                !ParseSemanticVersion(dependency.version, parsed) ||
                std::any_of(
                    dependencies.begin(),
                    dependencies.end(),
                    [&](const ValidatedPluginDependency& existing) {
                        return EqualInsensitive(existing.name, dependency.name);
                    }))
            {
                dependencies.clear();
                return false;
            }
            dependencies.push_back({
                dependency.name,
                dependency.version,
                dependency.requirement
            });
        }
    }
    catch (...)
    {
        dependencies.clear();
        return false;
    }
    return true;
}

}
