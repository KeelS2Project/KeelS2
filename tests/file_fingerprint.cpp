#include <keels2/platform/file_fingerprint.h>

#include <filesystem>
#include <fstream>
#include <string>

int main(int argument_count, char** arguments)
{
    if (argument_count != 2)
    {
        return 1;
    }

    const std::filesystem::path path = arguments[1];
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return 2;
        }
        stream << "KeelS2 fingerprint fixture\n";
    }

    keels2::platform::FileFingerprint fingerprint;
    std::string error;
    const bool fingerprinted = keels2::platform::FingerprintFile(path, fingerprint, error);
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    if (!fingerprinted || remove_error || fingerprint.size != 27 ||
        fingerprint.fnv1a64 != 0xcc9504a454c4f53aull ||
        keels2::platform::FormatFingerprint(fingerprint) != "size=27 fnv1a64=cc9504a454c4f53a")
    {
        return 3;
    }

    if (keels2::platform::FingerprintFile(path, fingerprint, error) || error.empty())
    {
        return 4;
    }

    return 0;
}
