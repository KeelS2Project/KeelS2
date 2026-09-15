#include <keels2/platform/dynamic_library.h>
#include <keels2/platform/loaded_module.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#else
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#endif

namespace keels2::platform
{

namespace
{

std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : normalized;
}

bool EqualPathText(std::string_view left, std::string_view right)
{
#if defined(_WIN32)
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(), [](unsigned char first, unsigned char second) {
            return std::tolower(first) == std::tolower(second);
        });
#else
    return left == right;
#endif
}

bool ContainsAddress(const ModuleMemoryRange& range, const void* address)
{
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    const auto start = reinterpret_cast<std::uintptr_t>(range.address);
    return value >= start && value - start < range.size;
}

bool ContainsSpan(
    const ModuleMemoryRange& range,
    const void* address,
    std::size_t size)
{
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    const auto start = reinterpret_cast<std::uintptr_t>(range.address);
    return value >= start && value - start <= range.size && size <= range.size - (value - start);
}

const ModuleMemoryRange* ReadableRange(
    const LoadedModule& module,
    const void* address,
    std::size_t size)
{
    const auto match = std::find_if(module.ranges.begin(), module.ranges.end(), [address, size](const auto& range) {
        return range.readable && ContainsSpan(range, address, size);
    });
    return match == module.ranges.end() ? nullptr : &*match;
}

#if defined(__clang__) || defined(__GNUC__)
#define KEELS2_NO_ADDRESS_SANITIZE __attribute__((no_sanitize_address))
#else
#define KEELS2_NO_ADDRESS_SANITIZE
#endif

KEELS2_NO_ADDRESS_SANITIZE
void CopyMappedBytes(void* destination, const void* source, std::size_t size)
{
    auto* output = static_cast<std::byte*>(destination);
    const auto* input = static_cast<const volatile std::byte*>(source);
    for (std::size_t index{}; index < size; ++index)
    {
        output[index] = input[index];
    }
}

KEELS2_NO_ADDRESS_SANITIZE
bool EqualMappedBytes(const std::byte* left, const std::byte* right, std::size_t size)
{
    const volatile std::byte* mapped = left;
    for (std::size_t index{}; index < size; ++index)
    {
        if (mapped[index] != right[index])
        {
            return false;
        }
    }
    return true;
}

#undef KEELS2_NO_ADDRESS_SANITIZE

template <typename Type>
bool ReadValue(const LoadedModule& module, const void* address, Type& value)
{
    if (!ReadableRange(module, address, sizeof(Type)))
    {
        return false;
    }
    CopyMappedBytes(&value, address, sizeof(value));
    return true;
}

std::vector<const std::byte*> FindBytes(
    const LoadedModule& module,
    const std::byte* pattern,
    std::size_t size)
{
    std::vector<const std::byte*> matches;
    if (!pattern || size == 0)
    {
        return matches;
    }
    for (const auto& range : module.ranges)
    {
        if (!range.readable || range.executable || range.size < size)
        {
            continue;
        }
        const auto limit = range.size - size;
        for (std::size_t offset{}; offset <= limit; ++offset)
        {
            if (EqualMappedBytes(range.address + offset, pattern, size))
            {
                matches.push_back(range.address + offset);
            }
        }
    }
    return matches;
}

template <typename Type>
std::vector<const std::byte*> FindAlignedValue(
    const LoadedModule& module,
    const Type& value)
{
    std::vector<const std::byte*> matches;
    for (const auto& range : module.ranges)
    {
        if (!range.readable || range.executable || range.size < sizeof(Type))
        {
            continue;
        }
        const auto start = reinterpret_cast<std::uintptr_t>(range.address);
        const auto aligned = (start + alignof(Type) - 1) & ~(alignof(Type) - 1);
        const auto skipped = aligned - start;
        if (skipped > range.size || range.size - skipped < sizeof(Type))
        {
            continue;
        }
        const auto count = (range.size - skipped - sizeof(Type)) / alignof(Type) + 1;
        for (std::size_t index{}; index < count; ++index)
        {
            const auto* address = reinterpret_cast<const std::byte*>(
                aligned + index * alignof(Type));
            Type candidate{};
            CopyMappedBytes(&candidate, address, sizeof(candidate));
            if (candidate == value)
            {
                matches.push_back(address);
            }
        }
    }
    return matches;
}

bool ValidClassName(std::string_view name)
{
    return !name.empty() && name.size() <= 256 &&
        name.find('\0') == std::string_view::npos &&
        std::all_of(name.begin(), name.end(), [](unsigned char value) {
            return std::isalnum(value) || value == '_';
        });
}

void AddVtableCandidate(
    const LoadedModule& module,
    const std::byte* address,
    std::size_t entry_count,
    std::vector<void**>& candidates)
{
    if (entry_count > std::numeric_limits<std::size_t>::max() / sizeof(void*) ||
        !ReadableRange(module, address, entry_count * sizeof(void*)))
    {
        return;
    }
    auto** table = reinterpret_cast<void**>(const_cast<std::byte*>(address));
    if (std::find(candidates.begin(), candidates.end(), table) == candidates.end())
    {
        candidates.push_back(table);
    }
}

#if defined(_WIN32)

std::vector<void**> FindWindowsVtables(
    const LoadedModule& module,
    std::string_view class_name,
    std::size_t entry_count)
{
    std::vector<void**> candidates;
    if (!module.base || module.image_size == 0)
    {
        return candidates;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module.base);
    const auto image_end = base + module.image_size;
    for (const char kind : {'V', 'U'})
    {
        const std::string decorated = ".?A" + std::string(1, kind) +
            std::string(class_name) + "@@";
        const auto names = FindBytes(
            module,
            reinterpret_cast<const std::byte*>(decorated.c_str()),
            decorated.size() + 1);
        for (const auto* name : names)
        {
            const auto name_value = reinterpret_cast<std::uintptr_t>(name);
            if (name_value < base + 2 * sizeof(void*))
            {
                continue;
            }
            const auto type_descriptor = name_value - 2 * sizeof(void*);
            if (type_descriptor < base || type_descriptor >= image_end ||
                type_descriptor - base > std::numeric_limits<std::uint32_t>::max())
            {
                continue;
            }
            void* spare{};
            if (!ReadValue(
                    module,
                    reinterpret_cast<const void*>(type_descriptor + sizeof(void*)),
                    spare) || spare)
            {
                continue;
            }
            const auto type_rva = static_cast<std::uint32_t>(type_descriptor - base);
            for (const auto* reference : FindAlignedValue(module, type_rva))
            {
                const auto reference_value = reinterpret_cast<std::uintptr_t>(reference);
                if (reference_value < base + 3 * sizeof(std::uint32_t))
                {
                    continue;
                }
                const auto locator = reference_value - 3 * sizeof(std::uint32_t);
                std::array<std::uint32_t, 6> fields{};
                if (!ReadValue(module, reinterpret_cast<const void*>(locator), fields) ||
                    fields[0] != 1 || fields[1] != 0 || fields[3] != type_rva ||
                    locator < base || locator - base > std::numeric_limits<std::uint32_t>::max() ||
                    fields[5] != static_cast<std::uint32_t>(locator - base))
                {
                    continue;
                }
                const auto locator_pointer = reinterpret_cast<const void*>(locator);
                for (const auto* vtable_reference : FindAlignedValue(module, locator_pointer))
                {
                    AddVtableCandidate(
                        module,
                        vtable_reference + sizeof(void*),
                        entry_count,
                        candidates);
                }
            }
        }
    }
    return candidates;
}

#else

std::vector<void**> FindItaniumVtables(
    const LoadedModule& module,
    std::string_view class_name,
    std::size_t entry_count)
{
    std::vector<void**> candidates;
    const std::string decorated = std::to_string(class_name.size()) + std::string(class_name);
    const auto names = FindBytes(
        module,
        reinterpret_cast<const std::byte*>(decorated.c_str()),
        decorated.size() + 1);
    for (const auto* name : names)
    {
        const auto name_pointer = reinterpret_cast<const void*>(name);
        for (const auto* name_reference : FindAlignedValue(module, name_pointer))
        {
            const auto reference_value = reinterpret_cast<std::uintptr_t>(name_reference);
            if (reference_value < sizeof(void*))
            {
                continue;
            }
            const auto type_info = reinterpret_cast<const void*>(reference_value - sizeof(void*));
            void* type_info_vtable{};
            if (!ReadValue(module, type_info, type_info_vtable) || !type_info_vtable)
            {
                continue;
            }
            for (const auto* type_reference : FindAlignedValue(module, type_info))
            {
                const auto type_reference_value = reinterpret_cast<std::uintptr_t>(type_reference);
                if (type_reference_value < sizeof(std::ptrdiff_t))
                {
                    continue;
                }
                std::ptrdiff_t offset_to_top{};
                if (!ReadValue(
                        module,
                        reinterpret_cast<const void*>(type_reference_value - sizeof(std::ptrdiff_t)),
                        offset_to_top) || offset_to_top != 0)
                {
                    continue;
                }
                AddVtableCandidate(
                    module,
                    type_reference + sizeof(void*),
                    entry_count,
                    candidates);
            }
        }
    }
    return candidates;
}

#endif

void SortRanges(LoadedModule& module)
{
    std::sort(module.ranges.begin(), module.ranges.end(), [](const auto& left, const auto& right) {
        return reinterpret_cast<std::uintptr_t>(left.address) <
            reinterpret_cast<std::uintptr_t>(right.address);
    });
}

#if defined(_WIN32)

bool ReadModuleRanges(LoadedModule& module)
{
    const auto* base = static_cast<const std::byte*>(module.base);
    if (!base || module.image_size < sizeof(IMAGE_DOS_HEADER))
    {
        return false;
    }
    const auto base_value = reinterpret_cast<std::uintptr_t>(base);
    if (module.image_size > std::numeric_limits<std::uintptr_t>::max() - base_value)
    {
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
    {
        return false;
    }
    const auto nt_offset = static_cast<std::size_t>(dos->e_lfanew);
    constexpr std::size_t nt_prefix_size = sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    if (nt_offset > module.image_size || nt_prefix_size > module.image_size - nt_offset)
    {
        return false;
    }
    const auto* signature = reinterpret_cast<const DWORD*>(base + nt_offset);
    if (*signature != IMAGE_NT_SIGNATURE)
    {
        return false;
    }
    const auto* file_header = reinterpret_cast<const IMAGE_FILE_HEADER*>(
        base + nt_offset + sizeof(DWORD));
    const auto optional_offset = nt_offset + nt_prefix_size;
    const auto optional_size = static_cast<std::size_t>(file_header->SizeOfOptionalHeader);
    if (optional_size < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        optional_size > module.image_size - optional_offset)
    {
        return false;
    }
    const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(base + optional_offset);
    const auto image_size = static_cast<std::size_t>(optional->SizeOfImage);
    if (optional->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || image_size == 0 ||
        image_size > module.image_size)
    {
        return false;
    }
    const auto section_offset = optional_offset + optional_size;
    const auto section_count = static_cast<std::size_t>(file_header->NumberOfSections);
    if (section_count > (module.image_size - section_offset) / sizeof(IMAGE_SECTION_HEADER))
    {
        return false;
    }
    module.ranges.clear();
    const auto* section = reinterpret_cast<const IMAGE_SECTION_HEADER*>(base + section_offset);
    for (std::size_t index{}; index < section_count; ++index, ++section)
    {
        const std::size_t size = std::max<std::size_t>(section->Misc.VirtualSize, section->SizeOfRawData);
        const std::size_t offset = section->VirtualAddress;
        if (size == 0 || offset >= image_size)
        {
            continue;
        }
        const auto characteristics = section->Characteristics;
        const std::size_t section_size = std::min(size, image_size - offset);
        const auto section_end = base_value + offset + section_size;
        for (std::uintptr_t cursor = base_value + offset; cursor < section_end;)
        {
            MEMORY_BASIC_INFORMATION memory{};
            if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) == 0 ||
                memory.RegionSize == 0)
            {
                return false;
            }
            const auto region_start = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
            if (memory.RegionSize > std::numeric_limits<std::uintptr_t>::max() - region_start)
            {
                return false;
            }
            const auto region_end = region_start + memory.RegionSize;
            const auto range_start = std::max(cursor, region_start);
            const auto range_end = std::min(section_end, region_end);
            const DWORD protection = memory.Protect & 0xffu;
            const bool guarded = (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0;
            const bool readable = !guarded && (protection == PAGE_READONLY ||
                protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
                protection == PAGE_EXECUTE_WRITECOPY);
            const bool executable = !guarded && (protection == PAGE_EXECUTE ||
                protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
                protection == PAGE_EXECUTE_WRITECOPY);
            if (memory.State == MEM_COMMIT && memory.AllocationBase == module.base && range_start < range_end)
            {
                module.ranges.push_back({
                    reinterpret_cast<const std::byte*>(range_start),
                    static_cast<std::size_t>(range_end - range_start),
                    readable && (characteristics & IMAGE_SCN_MEM_READ) != 0,
                    executable && (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0
                });
            }
            if (range_end <= cursor)
            {
                return false;
            }
            cursor = range_end;
        }
    }
    SortRanges(module);
    return !module.ranges.empty();
}

bool EnumerateModules(std::vector<LoadedModule>& modules, std::string& error)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        error = "could not enumerate loaded modules";
        return false;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry) == FALSE)
    {
        CloseHandle(snapshot);
        error = "could not read the loaded module list";
        return false;
    }
    do
    {
        LoadedModule module;
        module.path = NormalizePath(std::filesystem::path(entry.szExePath));
        module.base = entry.modBaseAddr;
        module.image_size = static_cast<std::size_t>(entry.modBaseSize);
        if (ReadModuleRanges(module))
        {
            modules.push_back(std::move(module));
        }
    }
    while (Module32NextW(snapshot, &entry) != FALSE);
    CloseHandle(snapshot);
    error.clear();
    return true;
}

#else

std::filesystem::path MainExecutablePath()
{
    std::string buffer(4096, '\0');
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0)
    {
        return {};
    }
    buffer.resize(static_cast<std::size_t>(length));
    return NormalizePath(buffer);
}

int CollectModule(dl_phdr_info* info, std::size_t, void* context)
{
    auto& modules = *static_cast<std::vector<LoadedModule>*>(context);
    LoadedModule module;
    module.path = info->dlpi_name && info->dlpi_name[0]
        ? NormalizePath(info->dlpi_name)
        : MainExecutablePath();
    module.base = reinterpret_cast<void*>(info->dlpi_addr);
    for (ElfW(Half) index{}; index < info->dlpi_phnum; ++index)
    {
        const auto& header = info->dlpi_phdr[index];
        if (header.p_type != PT_LOAD || header.p_memsz == 0)
        {
            continue;
        }
        module.ranges.push_back({
            reinterpret_cast<const std::byte*>(info->dlpi_addr + header.p_vaddr),
            static_cast<std::size_t>(header.p_memsz),
            (header.p_flags & PF_R) != 0,
            (header.p_flags & PF_X) != 0
        });
    }
    if (!module.path.empty() && !module.ranges.empty())
    {
        SortRanges(module);
        modules.push_back(std::move(module));
    }
    return 0;
}

bool EnumerateModules(std::vector<LoadedModule>& modules, std::string& error)
{
    if (dl_iterate_phdr(&CollectModule, &modules) < 0)
    {
        error = "could not enumerate loaded modules";
        return false;
    }
    error.clear();
    return true;
}

#endif

}

LoadedModulePin::LoadedModulePin(LoadedModulePin&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr))
{
}

LoadedModulePin& LoadedModulePin::operator=(LoadedModulePin&& other) noexcept
{
    if (this != &other)
    {
        Release();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

LoadedModulePin::~LoadedModulePin()
{
    Release();
}

bool LoadedModulePin::Acquire(LoadedModule& module, std::string& error)
{
    Release();
#if defined(_WIN32)
    const void* address = module.base;
    if (!address && !module.ranges.empty())
    {
        address = module.ranges.front().address;
    }
    HMODULE handle{};
    if (!address || !GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(address),
            &handle))
    {
        error = "loaded module could not be pinned";
        return false;
    }
    if (module.base && handle != module.base)
    {
        FreeLibrary(handle);
        error = "loaded module changed while it was being pinned";
        return false;
    }
    std::vector<LoadedModule> modules;
    if (!EnumerateModules(modules, error))
    {
        FreeLibrary(handle);
        return false;
    }
    const auto match = std::find_if(modules.begin(), modules.end(), [&module, handle](const auto& candidate) {
        return candidate.base == handle && candidate.path == module.path;
    });
    if (match == modules.end())
    {
        FreeLibrary(handle);
        error = "loaded module changed while it was being pinned";
        return false;
    }
    module = *match;
    handle_ = handle;
#else
    void* handle{};
    if (module.path == MainExecutablePath())
    {
        handle = dlopen(nullptr, RTLD_NOW);
    }
    else
    {
        handle = dlopen(module.path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    }
    if (!handle)
    {
        error = "loaded module could not be pinned";
        return false;
    }
    std::vector<LoadedModule> modules;
    if (!EnumerateModules(modules, error))
    {
        dlclose(handle);
        return false;
    }
    const auto match = std::find_if(modules.begin(), modules.end(), [&module](const auto& candidate) {
        return candidate.base == module.base && candidate.path == module.path;
    });
    if (match == modules.end())
    {
        dlclose(handle);
        error = "loaded module changed while it was being pinned";
        return false;
    }
    module = *match;
    handle_ = handle;
#endif
    error.clear();
    return true;
}

void LoadedModulePin::Release() noexcept
{
    if (!handle_)
    {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

bool LoadedModulePin::IsHeld() const noexcept
{
    return handle_ != nullptr;
}

ModuleLookup FindLoadedModule(
    std::string_view selector,
    LoadedModule& module,
    std::string& error)
{
    if (selector.empty())
    {
        error = "module selector is empty";
        return ModuleLookup::failed;
    }
    std::vector<LoadedModule> modules;
    if (!EnumerateModules(modules, error))
    {
        return ModuleLookup::failed;
    }

    const std::filesystem::path requested(selector);
    const bool exact_path = requested.is_absolute() || requested.has_parent_path();
    const auto normalized = exact_path ? NormalizePath(requested) : requested;
    std::vector<LoadedModule*> matches;
    for (auto& candidate : modules)
    {
        const auto actual = exact_path ? candidate.path : candidate.path.filename();
        if (EqualPathText(actual.string(), normalized.string()))
        {
            matches.push_back(&candidate);
        }
    }
    if (matches.empty())
    {
        error = "loaded module was not found: " + std::string(selector);
        return ModuleLookup::not_found;
    }
    if (matches.size() != 1)
    {
        error = "loaded module selector is ambiguous: " + std::string(selector);
        return ModuleLookup::ambiguous;
    }
    module = std::move(*matches.front());
    error.clear();
    return ModuleLookup::found;
}

ModuleLookup FindLoadedModuleForAddress(
    const void* address,
    LoadedModule& module,
    std::string& error)
{
    if (!address)
    {
        error = "module address is null";
        return ModuleLookup::failed;
    }
    std::vector<LoadedModule> modules;
    if (!EnumerateModules(modules, error))
    {
        return ModuleLookup::failed;
    }
    for (auto& candidate : modules)
    {
        if (std::any_of(candidate.ranges.begin(), candidate.ranges.end(), [address](const auto& range) {
                return ContainsAddress(range, address);
            }))
        {
            module = std::move(candidate);
            error.clear();
            return ModuleLookup::found;
        }
    }
    error = "address does not belong to a loaded module";
    return ModuleLookup::not_found;
}

ModuleLookup FindPrimaryVtable(
    const LoadedModule& module,
    std::string_view class_name,
    std::size_t entry_count,
    void**& table,
    std::string& error)
{
    table = nullptr;
    if (!ValidClassName(class_name) || entry_count == 0 || module.ranges.empty())
    {
        error = "virtual table request is invalid";
        return ModuleLookup::failed;
    }
#if defined(_WIN32)
    auto candidates = FindWindowsVtables(module, class_name, entry_count);
#else
    auto candidates = FindItaniumVtables(module, class_name, entry_count);
#endif
    if (candidates.empty())
    {
        error = "primary virtual table was not found for " + std::string(class_name);
        return ModuleLookup::not_found;
    }
    if (candidates.size() != 1)
    {
        error = "primary virtual table is ambiguous for " + std::string(class_name);
        return ModuleLookup::ambiguous;
    }
    table = candidates.front();
    error.clear();
    return ModuleLookup::found;
}

void* FindLoadedSymbol(
    const LoadedModule& module,
    std::string_view symbol,
    std::string& error)
{
    if (symbol.empty() || symbol.find('\0') != std::string_view::npos)
    {
        error = "symbol request is invalid";
        return nullptr;
    }
    const std::string name(symbol);
#if defined(_WIN32)
    if (!module.base)
    {
        error = "symbol request is invalid";
        return nullptr;
    }
    const FARPROC procedure = GetProcAddress(static_cast<HMODULE>(module.base), name.c_str());
    if (!procedure)
    {
        error = "symbol was not found: " + name;
        return nullptr;
    }
    static_assert(sizeof(procedure) == sizeof(void*));
    void* address{};
    std::memcpy(&address, &procedure, sizeof(address));
#else
    void* handle = dlopen(module.path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (!handle)
    {
        const auto executable = MainExecutablePath();
        if (module.path == executable)
        {
            handle = dlopen(nullptr, RTLD_NOW);
        }
    }
    if (!handle)
    {
        error = "could not inspect loaded module symbols";
        return nullptr;
    }
    dlerror();
    void* address = dlsym(handle, name.c_str());
    const char* symbol_error = dlerror();
    dlclose(handle);
    if (symbol_error || !address)
    {
        error = "symbol was not found: " + name;
        return nullptr;
    }
#endif
    LoadedModule owner;
    std::string owner_error;
    if (FindLoadedModuleForAddress(address, owner, owner_error) != ModuleLookup::found ||
        owner.base != module.base)
    {
        error = "symbol resolves outside the requested module: " + name;
        return nullptr;
    }
    error.clear();
    return address;
}

bool IsExecutableAddress(const LoadedModule& module, const void* address)
{
    return std::any_of(module.ranges.begin(), module.ranges.end(), [address](const auto& range) {
        return range.readable && range.executable && ContainsAddress(range, address);
    });
}

}
