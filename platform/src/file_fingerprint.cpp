#include <keels2/platform/file_fingerprint.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace keels2::platform
{

bool FingerprintFile(const std::filesystem::path& path, FileFingerprint& fingerprint, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "could not open the file";
        return false;
    }

    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::array<char, 65536> buffer{};
    FileFingerprint result{0, offset};

    while (stream)
    {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = stream.gcount();
        for (std::streamsize index = 0; index < count; ++index)
        {
            result.fnv1a64 ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            result.fnv1a64 *= prime;
        }
        result.size += static_cast<std::uint64_t>(count);
    }

    if (!stream.eof())
    {
        error = "could not read the complete file";
        return false;
    }

    fingerprint = result;
    error.clear();
    return true;
}
std::string FormatFingerprint(const FileFingerprint& fingerprint)
{
    std::ostringstream stream;
    stream << "size=" << fingerprint.size << " fnv1a64="
           << std::hex << std::setw(16) << std::setfill('0') << fingerprint.fnv1a64;
    return stream.str();
}

}
