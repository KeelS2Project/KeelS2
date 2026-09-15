#ifndef KEELS2_PLATFORM_LOADED_MODULE_H
#define KEELS2_PLATFORM_LOADED_MODULE_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace keels2::platform
{

struct ModuleMemoryRange
{
    const std::byte* address{};
    std::size_t size{};
    bool readable{};
    bool executable{};
};

struct LoadedModule
{
    std::filesystem::path path;
    void* base{};
    std::size_t image_size{};
    std::vector<ModuleMemoryRange> ranges;
};

class LoadedModulePin
{
public:
    LoadedModulePin() = default;
    LoadedModulePin(const LoadedModulePin&) = delete;
    LoadedModulePin& operator=(const LoadedModulePin&) = delete;
    LoadedModulePin(LoadedModulePin&& other) noexcept;
    LoadedModulePin& operator=(LoadedModulePin&& other) noexcept;
    ~LoadedModulePin();

    bool Acquire(LoadedModule& module, std::string& error);
    void Release() noexcept;
    bool IsHeld() const noexcept;

private:
    void* handle_{};
};

enum class ModuleLookup
{
    found,
    not_found,
    ambiguous,
    failed
};

ModuleLookup FindLoadedModule(
    std::string_view selector,
    LoadedModule& module,
    std::string& error);
ModuleLookup FindLoadedModuleForAddress(
    const void* address,
    LoadedModule& module,
    std::string& error);
ModuleLookup FindPrimaryVtable(
    const LoadedModule& module,
    std::string_view class_name,
    std::size_t entry_count,
    void**& table,
    std::string& error);
void* FindLoadedSymbol(
    const LoadedModule& module,
    std::string_view symbol,
    std::string& error);
bool IsExecutableAddress(const LoadedModule& module, const void* address);

}

#endif
