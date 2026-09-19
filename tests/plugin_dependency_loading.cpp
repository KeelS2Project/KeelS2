#include <keels2/platform/dynamic_library.h>
#include <cstring>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error("consumer dependency fixture-directory");

        const auto root = std::filesystem::absolute(argv[3]);
        const auto libraries = root / "plugins/lib";
        const auto staged = root / "plugins/.runtime/17";
        std::filesystem::create_directories(libraries);
        std::filesystem::create_directories(staged);
        const auto library = libraries / std::filesystem::path(argv[2]).filename();
        const auto plugin = staged / std::filesystem::path(argv[1]).filename();
        std::filesystem::copy_file(argv[1], plugin, std::filesystem::copy_options::overwrite_existing);
        keels2::platform::DynamicLibrary first, second;
        std::string error;
        std::filesystem::remove(library);

        if (first.OpenWithDependencies(plugin, libraries, error))
            throw std::runtime_error("missing staged dependency unexpectedly found elsewhere");

        std::filesystem::copy_file(argv[2], library, std::filesystem::copy_options::overwrite_existing);

        if (!first.OpenWithDependencies(plugin, libraries, error))
            throw std::runtime_error(error);

        const auto second_path = root / "plugins/.runtime/18" / plugin.filename();
        std::filesystem::create_directories(second_path.parent_path());
        std::filesystem::copy_file(plugin, second_path, std::filesystem::copy_options::overwrite_existing);

        if (!second.OpenWithDependencies(second_path, libraries, error))
            throw std::runtime_error(error);

        auto raw = second.Symbol("KeelDependencyConsumer");

        int (*invoke)(){};
        static_assert(sizeof(invoke) == sizeof(raw));
        std::memcpy(&invoke, &raw, sizeof(invoke));
        first.Close();

        if (!invoke || invoke() != 42)
            throw std::runtime_error("shared dependency lost before last consumer");

        second.Close();
        std::cout << "staged plugin shared dependency lookup passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
