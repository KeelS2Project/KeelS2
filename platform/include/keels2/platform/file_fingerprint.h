#ifndef KEELS2_PLATFORM_FILE_FINGERPRINT_H
#define KEELS2_PLATFORM_FILE_FINGERPRINT_H

#include <cstdint>
#include <filesystem>
#include <string>

namespace keels2::platform
{

struct FileFingerprint
{
    std::uint64_t size{};
    std::uint64_t fnv1a64{};

    bool operator==(const FileFingerprint&) const = default;
};

bool FingerprintFile(const std::filesystem::path& path, FileFingerprint& fingerprint, std::string& error);
std::string FormatFingerprint(const FileFingerprint& fingerprint);

}

#endif
