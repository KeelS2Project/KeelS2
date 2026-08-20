#include <keels2/platform/dynamic_library.h>

#include <cstring>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace keels2::platform
{

#if defined(_WIN32)

static std::string WindowsError(DWORD code)
{
    char* message{};
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<char*>(&message),
        0,
        nullptr
    );

    std::string result = length && message ? std::string(message, length) : "Windows error " + std::to_string(code);
    if (message)
    {
        LocalFree(message);
    }
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
    {
        result.pop_back();
    }
    return result;
}

#endif

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept : handle_(std::exchange(other.handle_, nullptr))
{
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
{
    if (this != &other)
    {
        Close();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary()
{
    Close();
}

bool DynamicLibrary::Open(const std::filesystem::path& path, std::string& error)
{
    Close();

#if defined(_WIN32)
    handle_ = reinterpret_cast<void*>(LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
    if (!handle_)
    {
        error = WindowsError(GetLastError());
        return false;
    }
#else
    dlerror();
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_)
    {
        const char* message = dlerror();
        error = message ? message : "dlopen failed without an error message";
        return false;
    }
#endif

    error.clear();
    return true;
}

void DynamicLibrary::Close()
{
    if (!handle_)
    {
        return;
    }

#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

void* DynamicLibrary::Symbol(const char* name) const
{
    if (!handle_ || !name)
    {
        return nullptr;
    }

#if defined(_WIN32)
    const FARPROC function = GetProcAddress(reinterpret_cast<HMODULE>(handle_), name);
    static_assert(sizeof(function) == sizeof(void*));
    void* address{};
    std::memcpy(&address, &function, sizeof(address));
    return address;
#else
    return dlsym(handle_, name);
#endif
}

bool DynamicLibrary::IsOpen() const
{
    return handle_ != nullptr;
}

bool ModulePathFromAddress(const void* address, std::filesystem::path& path, std::string& error)
{
    if (!address)
    {
        error = "module address is null";
        return false;
    }

#if defined(_WIN32)
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),
            &module))
    {
        error = WindowsError(GetLastError());
        return false;
    }

    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length == buffer.size())
    {
        error = WindowsError(GetLastError());
        return false;
    }
    buffer.resize(length);
    path = std::filesystem::path(buffer);
#else
    Dl_info info{};
    if (!dladdr(address, &info) || !info.dli_fname)
    {
        error = "dladdr could not resolve the module path";
        return false;
    }
    path = std::filesystem::path(info.dli_fname);
#endif

    error.clear();
    return true;
}

void* ModuleSymbolFromAddress(const void* address, const char* name, std::string& error)
{
    if (!address || !name || !name[0])
    {
        error = "module symbol request is invalid";
        return nullptr;
    }

#if defined(_WIN32)
    HMODULE module{};
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),
            &module))
    {
        error = WindowsError(GetLastError());
        return nullptr;
    }
    const FARPROC function = GetProcAddress(module, name);
    if (!function)
    {
        error = WindowsError(GetLastError());
        return nullptr;
    }
    static_assert(sizeof(function) == sizeof(void*));
    void* result{};
    std::memcpy(&result, &function, sizeof(result));
#else
    Dl_info info{};
    if (!dladdr(address, &info) || !info.dli_fname)
    {
        error = "dladdr could not resolve the module";
        return nullptr;
    }

    dlerror();
    void* module = dlopen(info.dli_fname, RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD);
    bool close_module = module != nullptr;
    if (!module)
    {
        dlerror();
        module = dlopen(nullptr, RTLD_NOW | RTLD_LOCAL);
        close_module = module != nullptr;
    }
    if (!module)
    {
        const char* message = dlerror();
        error = message ? message : "dlopen failed without an error message";
        return nullptr;
    }

    dlerror();
    void* result = dlsym(module, name);
    const char* message = dlerror();
    if (close_module)
    {
        dlclose(module);
    }
    if (message || !result)
    {
        error = message ? message : "module symbol is unavailable";
        return nullptr;
    }
#endif

    error.clear();
    return result;
}

}
