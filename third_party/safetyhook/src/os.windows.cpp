#include <algorithm>
#include <limits>
#include <mutex>
#include <vector>

#include "safetyhook/common.hpp"
#include "safetyhook/utility.hpp"

#if SAFETYHOOK_OS_WINDOWS

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if __has_include(<Windows.h>)
#include <Windows.h>
#include <TlHelp32.h>
#elif __has_include(<windows.h>)
#include <windows.h>
#include <tlhelp32.h>
#else
#error "Windows.h not found"
#endif

#include "safetyhook/os.hpp"

namespace safetyhook {
std::expected<uint8_t*, OsError> vm_allocate(uint8_t* address, size_t size, VmAccess access) {
    DWORD protect = 0;

    if (access == VM_ACCESS_R) {
        protect = PAGE_READONLY;
    } else if (access == VM_ACCESS_RW) {
        protect = PAGE_READWRITE;
    } else if (access == VM_ACCESS_RX) {
        protect = PAGE_EXECUTE_READ;
    } else if (access == VM_ACCESS_RWX) {
        protect = PAGE_EXECUTE_READWRITE;
    } else {
        return std::unexpected{OsError::FAILED_TO_ALLOCATE};
    }

    auto* result = VirtualAlloc(address, size, MEM_COMMIT | MEM_RESERVE, protect);

    if (result == nullptr) {
        return std::unexpected{OsError::FAILED_TO_ALLOCATE};
    }

    return static_cast<uint8_t*>(result);
}

void vm_free(uint8_t* address, [[maybe_unused]] size_t size) {
    VirtualFree(address, 0, MEM_RELEASE);
}

std::expected<uint32_t, OsError> vm_protect(uint8_t* address, size_t size, VmAccess access) {
    DWORD protect = 0;

    if (access == VM_ACCESS_R) {
        protect = PAGE_READONLY;
    } else if (access == VM_ACCESS_RW) {
        protect = PAGE_READWRITE;
    } else if (access == VM_ACCESS_RX) {
        protect = PAGE_EXECUTE_READ;
    } else if (access == VM_ACCESS_RWX) {
        protect = PAGE_EXECUTE_READWRITE;
    } else {
        return std::unexpected{OsError::FAILED_TO_PROTECT};
    }

    return vm_protect(address, size, protect);
}

std::expected<uint32_t, OsError> vm_protect(uint8_t* address, size_t size, uint32_t protect) {
    DWORD old_protect = 0;

    if (VirtualProtect(address, size, protect, &old_protect) == FALSE) {
        return std::unexpected{OsError::FAILED_TO_PROTECT};
    }

    return old_protect;
}

std::expected<VmBasicInfo, OsError> vm_query(uint8_t* address) {
    MEMORY_BASIC_INFORMATION mbi{};
    auto result = VirtualQuery(address, &mbi, sizeof(mbi));

    if (result == 0) {
        return std::unexpected{OsError::FAILED_TO_QUERY};
    }

    VmAccess access{};
    access.read = (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;
    access.write = (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) != 0;
    access.execute = (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;

    VmBasicInfo info{};
    info.address = static_cast<uint8_t*>(mbi.AllocationBase);
    info.size = mbi.RegionSize;
    info.access = access;
    info.is_free = mbi.State == MEM_FREE;

    return info;
}

bool vm_is_readable(uint8_t* address, size_t size) {
    return IsBadReadPtr(address, size) == FALSE;
}

bool vm_is_writable(uint8_t* address, size_t size) {
    return IsBadWritePtr(address, size) == FALSE;
}

bool vm_is_executable(uint8_t* address) {
    // Check if the address is in a valid module allowing us to potentially skip a heavier memory query.
    HMODULE image{};
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPTSTR>(address), &image) ||
        image == nullptr) {
        return vm_query(address).value_or(VmBasicInfo{}).access.execute;
    }

    // Just check if the section is executable.
    const auto* image_base = reinterpret_cast<uint8_t*>(image);
    const auto* dos_hdr = reinterpret_cast<const IMAGE_DOS_HEADER*>(image_base);

    if (dos_hdr->e_magic != IMAGE_DOS_SIGNATURE) {
        return vm_query(address).value_or(VmBasicInfo{}).access.execute;
    }

    const auto* nt_hdr = reinterpret_cast<const IMAGE_NT_HEADERS*>(image_base + dos_hdr->e_lfanew);

    if (nt_hdr->Signature != IMAGE_NT_SIGNATURE) {
        return vm_query(address).value_or(VmBasicInfo{}).access.execute;
    }

    const auto* section = IMAGE_FIRST_SECTION(nt_hdr);

    for (auto i = 0; i < nt_hdr->FileHeader.NumberOfSections; ++i, ++section) {
        if (address >= image_base + section->VirtualAddress &&
            address < image_base + section->VirtualAddress + section->Misc.VirtualSize) {
            return (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        }
    }

    return vm_query(address).value_or(VmBasicInfo{}).access.execute;
}

SystemInfo system_info() {
    SystemInfo info{};

    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    info.page_size = si.dwPageSize;
    info.allocation_granularity = si.dwAllocationGranularity;
    info.min_address = static_cast<uint8_t*>(si.lpMinimumApplicationAddress);
    info.max_address = static_cast<uint8_t*>(si.lpMaximumApplicationAddress);

    return info;
}

namespace {

struct FrozenThread {
    DWORD id{};
    HANDLE handle{};
    CONTEXT context{};
};

struct PatchRegion {
    uint8_t* address{};
    SIZE_T size{};
    DWORD protection{};
};

std::mutex g_trap_mutex;

uintptr_t context_ip(const CONTEXT& context) {
#if SAFETYHOOK_ARCH_X86_64
    return context.Rip;
#elif SAFETYHOOK_ARCH_X86_32
    return context.Eip;
#endif
}

bool address_in_range(uint8_t* address, const IpRange& range) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(range.address);
    return value >= begin && value - begin < range.size;
}

bool enumerate_threads(std::vector<DWORD>& threads) {
    threads.clear();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    BOOL present = Thread32First(snapshot, &entry);
    while (present) {
        if (entry.th32OwnerProcessID == GetCurrentProcessId()) {
            if (threads.size() == threads.capacity()) {
                CloseHandle(snapshot);
                return false;
            }
            threads.push_back(entry.th32ThreadID);
        }
        present = Thread32Next(snapshot, &entry);
    }
    const DWORD error = GetLastError();
    CloseHandle(snapshot);
    if (error != ERROR_NO_MORE_FILES) {
        return false;
    }
    std::sort(threads.begin(), threads.end());
    threads.erase(std::unique(threads.begin(), threads.end()), threads.end());
    return true;
}

FrozenThread* frozen_thread(std::vector<FrozenThread>& frozen, DWORD id) {
    auto found = std::find_if(frozen.begin(), frozen.end(),
        [id](const FrozenThread& thread) { return thread.id == id; });
    return found == frozen.end() ? nullptr : &*found;
}

void resume_threads(std::vector<FrozenThread>& frozen) {
    for (auto iterator = frozen.rbegin(); iterator != frozen.rend(); ++iterator) {
        ResumeThread(iterator->handle);
        CloseHandle(iterator->handle);
        iterator->handle = nullptr;
    }
}

bool executable_protection(DWORD protection) {
    const DWORD base = protection & 0xffu;
    return (protection & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
        (base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ ||
            base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY);
}

bool read_patch_regions(uint8_t* address, size_t size, std::vector<PatchRegion>& regions) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
    if (size > std::numeric_limits<uintptr_t>::max() - begin) {
        return false;
    }
    const uintptr_t end = begin + size;
    for (uintptr_t cursor = begin; cursor < end;) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) == 0 ||
            memory.RegionSize == 0 || memory.State != MEM_COMMIT || !executable_protection(memory.Protect)) {
            return false;
        }
        const uintptr_t region_begin = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize > std::numeric_limits<uintptr_t>::max() - region_begin) {
            return false;
        }
        const uintptr_t region_end = region_begin + memory.RegionSize;
        const uintptr_t range_end = std::min(end, region_end);
        if (range_end <= cursor) {
            return false;
        }
        regions.push_back({
            reinterpret_cast<uint8_t*>(cursor),
            static_cast<SIZE_T>(range_end - cursor),
            memory.Protect,
        });
        cursor = range_end;
    }
    return !regions.empty();
}

}

bool trap_threads(uint8_t* patch_address, size_t patch_size,
    [[maybe_unused]] std::span<const IpMapping> mappings, std::span<const IpRange> hazardous_ranges,
    const std::function<void()>& run_fn) {
    if (!patch_address || patch_size == 0 || !run_fn) {
        return false;
    }

    std::scoped_lock lock{g_trap_mutex};
    if (address_in_range(reinterpret_cast<uint8_t*>(&trap_threads), IpRange{patch_address, patch_size})) {
        return false;
    }

    std::vector<PatchRegion> regions;
    if (!read_patch_regions(patch_address, patch_size, regions)) {
        return false;
    }

    const DWORD current = GetCurrentThreadId();
    for (size_t attempt{}; attempt < 256; ++attempt) {
        std::vector<DWORD> threads;
        std::vector<FrozenThread> frozen;
        threads.reserve(8192);
        frozen.reserve(8192);
        bool stable{};
        for (size_t pass{}; pass < 64; ++pass) {
            if (!enumerate_threads(threads)) {
                resume_threads(frozen);
                return false;
            }
            bool added{};
            for (DWORD id : threads) {
                if (id == current || frozen_thread(frozen, id)) {
                    continue;
                }
                HANDLE handle = OpenThread(
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                    FALSE, id);
                if (!handle) {
                    if (GetLastError() == ERROR_INVALID_PARAMETER) {
                        continue;
                    }
                    resume_threads(frozen);
                    return false;
                }
                if (SuspendThread(handle) == static_cast<DWORD>(-1)) {
                    CloseHandle(handle);
                    resume_threads(frozen);
                    return false;
                }
                FrozenThread thread{};
                thread.id = id;
                thread.handle = handle;
                thread.context.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(handle, &thread.context) == FALSE) {
                    ResumeThread(handle);
                    CloseHandle(handle);
                    resume_threads(frozen);
                    return false;
                }
                if (frozen.size() == frozen.capacity()) {
                    ResumeThread(handle);
                    CloseHandle(handle);
                    resume_threads(frozen);
                    return false;
                }
                frozen.push_back(thread);
                added = true;
            }
            if (!added) {
                stable = true;
                break;
            }
        }
        if (!stable) {
            resume_threads(frozen);
            return false;
        }

        const bool hazardous = std::any_of(frozen.begin(), frozen.end(), [&hazardous_ranges](const auto& thread) {
            auto* ip = reinterpret_cast<uint8_t*>(context_ip(thread.context));
            return std::any_of(hazardous_ranges.begin(), hazardous_ranges.end(), [ip](const auto& range) {
                return address_in_range(ip, range);
            });
        });
        if (hazardous) {
            resume_threads(frozen);
            SwitchToThread();
            continue;
        }

        size_t protected_count{};
        for (; protected_count < regions.size(); ++protected_count) {
            DWORD previous{};
            if (VirtualProtect(
                    regions[protected_count].address,
                    regions[protected_count].size,
                    PAGE_EXECUTE_READWRITE,
                    &previous) == FALSE) {
                while (protected_count > 0) {
                    --protected_count;
                    DWORD ignored{};
                    VirtualProtect(
                        regions[protected_count].address,
                        regions[protected_count].size,
                        regions[protected_count].protection,
                        &ignored);
                }
                resume_threads(frozen);
                return false;
            }
            regions[protected_count].protection = previous;
        }

        run_fn();
        FlushInstructionCache(GetCurrentProcess(), patch_address, patch_size);
        for (auto iterator = regions.rbegin(); iterator != regions.rend(); ++iterator) {
            DWORD ignored{};
            if (VirtualProtect(iterator->address, iterator->size, iterator->protection, &ignored) == FALSE) {
                VirtualProtect(iterator->address, iterator->size, PAGE_EXECUTE_READ, &ignored);
            }
        }
        resume_threads(frozen);
        return true;
    }
    return false;
}

void fix_ip(ThreadContext thread_ctx, uint8_t* old_ip, uint8_t* new_ip) {
    auto* ctx = reinterpret_cast<CONTEXT*>(thread_ctx);

#if SAFETYHOOK_ARCH_X86_64
    auto ip = ctx->Rip;
#elif SAFETYHOOK_ARCH_X86_32
    auto ip = ctx->Eip;
#endif

    if (ip == reinterpret_cast<uintptr_t>(old_ip)) {
        ip = reinterpret_cast<uintptr_t>(new_ip);
    }

#if SAFETYHOOK_ARCH_X86_64
    ctx->Rip = ip;
#elif SAFETYHOOK_ARCH_X86_32
    ctx->Eip = ip;
#endif
}

} // namespace safetyhook

#endif
