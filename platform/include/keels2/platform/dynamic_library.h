#ifndef KEELS2_PLATFORM_DYNAMIC_LIBRARY_H
#define KEELS2_PLATFORM_DYNAMIC_LIBRARY_H

#include <filesystem>
#include <string>

namespace keels2::platform
{

class DynamicLibrary
{
public:
    DynamicLibrary() = default;
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    ~DynamicLibrary();

    bool Open(const std::filesystem::path& path, std::string& error);
    void Close();
    void* Symbol(const char* name) const;
    bool IsOpen() const;

private:
    void* handle_{};
};

bool ModulePathFromAddress(const void* address, std::filesystem::path& path, std::string& error);
void* ModuleSymbolFromAddress(const void* address, const char* name, std::string& error);

}

#endif
