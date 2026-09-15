#ifndef KEELS2_HOST_PLUGIN_MANIFEST_VALIDATION_H
#define KEELS2_HOST_PLUGIN_MANIFEST_VALIDATION_H

#include <keels2/plugin.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace keels2::host
{

struct ValidatedPluginDependency
{
    std::string name;
    std::string version;
    KeelPluginDependencyRequirement requirement{};
};

bool ValidMetadataText(const char* text, std::size_t maximum, bool allow_empty) noexcept;
bool ValidPluginName(const char* name) noexcept;
bool ParseSemanticVersion(
    std::string_view version,
    std::array<std::uint32_t, 3>& output) noexcept;
bool ValidatePluginManifest(
    const KeelPluginManifest& manifest,
    std::vector<ValidatedPluginDependency>& dependencies) noexcept;

}

#endif
