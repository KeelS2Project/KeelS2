#include <keels2/platform/file_fingerprint.h>

#include <filesystem>
#include <iostream>
#include <string>

int main(int argument_count, char** arguments)
{
    if (argument_count < 2)
    {
        std::cerr << "usage: keels2_compatibility_fingerprint <module> [module...]\n";
        return 1;
    }

    int result{};
    for (int index = 1; index < argument_count; ++index)
    {
        const std::filesystem::path path = arguments[index];
        keels2::platform::FileFingerprint fingerprint;
        std::string error;
        if (!keels2::platform::FingerprintFile(path, fingerprint, error))
        {
            std::cerr << path.string() << ": " << error << '\n';
            result = 2;
            continue;
        }
        std::cout << path.string() << ": " << keels2::platform::FormatFingerprint(fingerprint) << '\n';
    }
    return result;
}
