#include "safetyhook/common.hpp"

#if SAFETYHOOK_OS_LINUX

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include "safetyhook/utility.hpp"

#include "safetyhook/os.hpp"

namespace safetyhook {
namespace {

enum : uint8_t {
    THREAD_PENDING,
    THREAD_CLAIMED,
    THREAD_PARKED,
    THREAD_DEPARTED,
    THREAD_GONE,
};

struct ThreadSlot {
    pid_t tid{};
    pid_t proc_tid{};
    std::atomic<uint8_t> status{THREAD_PENDING};
    std::atomic<void*> context{};
};

struct RendezvousState {
    static constexpr size_t capacity = 8192;

    int signal_number{};
    std::unique_ptr<ThreadSlot[]> slots{std::make_unique<ThreadSlot[]>(capacity)};
    std::atomic<size_t> count{};
    std::atomic<bool> release{};
};

struct ProtectedPage {
    uint8_t* address;
    size_t size;
    int protection;
};

struct ThreadIdentity {
    pid_t tid;
    pid_t proc_tid;
};

std::mutex g_trap_mutex;
std::atomic<RendezvousState*> g_rendezvous{};
std::atomic<bool> g_poisoned{};

static_assert(std::atomic<RendezvousState*>::is_always_lock_free);
static_assert(std::atomic<void*>::is_always_lock_free);
static_assert(std::atomic<size_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<uint8_t>::is_always_lock_free);

uint8_t* instruction_pointer(void* context) {
    auto* machine = static_cast<ucontext_t*>(context);
#if SAFETYHOOK_ARCH_X86_64
    return reinterpret_cast<uint8_t*>(machine->uc_mcontext.gregs[REG_RIP]);
#elif SAFETYHOOK_ARCH_X86_32
    return reinterpret_cast<uint8_t*>(machine->uc_mcontext.gregs[REG_EIP]);
#endif
}

void set_instruction_pointer(void* context, uint8_t* address) {
    auto* machine = static_cast<ucontext_t*>(context);
#if SAFETYHOOK_ARCH_X86_64
    machine->uc_mcontext.gregs[REG_RIP] = reinterpret_cast<greg_t>(address);
#elif SAFETYHOOK_ARCH_X86_32
    machine->uc_mcontext.gregs[REG_EIP] = reinterpret_cast<greg_t>(address);
#endif
}

void rendezvous_signal(int signal, siginfo_t*, void* context) {
    RendezvousState* state = g_rendezvous.load(std::memory_order_acquire);
    if (!state || signal != state->signal_number) {
        return;
    }

    const auto tid = static_cast<pid_t>(syscall(SYS_gettid));
    const size_t count = state->count.load(std::memory_order_acquire);
    for (size_t index{}; index < count; ++index) {
        ThreadSlot& slot = state->slots[index];
        if (slot.tid != tid) {
            continue;
        }
        uint8_t expected = THREAD_PENDING;
        if (!slot.status.compare_exchange_strong(
                expected, THREAD_CLAIMED, std::memory_order_acq_rel, std::memory_order_acquire)) {
            continue;
        }
        slot.context.store(context, std::memory_order_release);
        slot.status.store(THREAD_PARKED, std::memory_order_release);
        while (!state->release.load(std::memory_order_acquire)) {
#if SAFETYHOOK_ARCH_X86_64 || SAFETYHOOK_ARCH_X86_32
            __asm__ volatile("pause");
#endif
        }
        slot.status.store(THREAD_DEPARTED, std::memory_order_release);
        return;
    }
}

bool append_text(char* destination, size_t capacity, size_t& length, const char* text) {
    for (size_t index{}; text[index] != '\0'; ++index) {
        if (length + 1 >= capacity) {
            return false;
        }
        destination[length++] = text[index];
    }
    destination[length] = '\0';
    return true;
}

bool append_pid(char* destination, size_t capacity, size_t& length, pid_t value) {
    if (value <= 0) {
        return false;
    }
    char digits[32];
    size_t count{};
    for (auto remaining = static_cast<unsigned long>(value); remaining != 0; remaining /= 10) {
        digits[count++] = static_cast<char>('0' + remaining % 10);
    }
    if (count == 0 || length + count >= capacity) {
        return false;
    }
    while (count != 0) {
        destination[length++] = digits[--count];
    }
    destination[length] = '\0';
    return true;
}

bool task_status(pid_t proc_tid, char* buffer, size_t capacity, size_t& length, bool& gone) {
    gone = false;
    length = 0;
    char path[96]{};
    size_t path_length{};
    if (!append_text(path, sizeof(path), path_length, "/proc/self/task/") ||
        !append_pid(path, sizeof(path), path_length, proc_tid) ||
        !append_text(path, sizeof(path), path_length, "/status")) {
        return false;
    }
    const int descriptor = static_cast<int>(syscall(
        SYS_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0));
    if (descriptor < 0) {
        gone = errno == ENOENT;
        return false;
    }
    bool success = capacity != 0;
    while (success && length + 1 < capacity) {
        const auto result = static_cast<ssize_t>(
            syscall(SYS_read, descriptor, buffer + length, capacity - length - 1));
        if (result > 0) {
            length += static_cast<size_t>(result);
            continue;
        }
        if (result == 0) {
            break;
        }
        if (errno != EINTR) {
            success = false;
        }
    }
    if (capacity != 0) {
        buffer[length] = '\0';
    }
    if (syscall(SYS_close, descriptor) != 0) {
        success = false;
    }
    return success;
}

std::optional<size_t> line_value(
    const char* buffer, size_t length, const char* label, size_t label_length) {
    for (size_t index{}; index + label_length <= length; ++index) {
        if (index != 0 && buffer[index - 1] != '\n') {
            continue;
        }
        bool matches = true;
        for (size_t offset{}; offset < label_length; ++offset) {
            if (buffer[index + offset] != label[offset]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return index + label_length;
        }
    }
    return std::nullopt;
}

bool namespace_tid(pid_t proc_tid, pid_t& tid, bool& gone) {
    char status[8192];
    size_t length{};
    if (!task_status(proc_tid, status, sizeof(status), length, gone)) {
        return false;
    }
    const auto value_offset = line_value(status, length, "NSpid:", 6);
    if (!value_offset) {
        tid = proc_tid;
        return true;
    }
    unsigned long last{};
    bool found{};
    for (size_t index = *value_offset; index < length && status[index] != '\n';) {
        if (status[index] < '0' || status[index] > '9') {
            ++index;
            continue;
        }
        unsigned long value{};
        do {
            const auto digit = static_cast<unsigned long>(status[index] - '0');
            if (value > (std::numeric_limits<unsigned long>::max() - digit) / 10) {
                return false;
            }
            value = value * 10 + digit;
            ++index;
        } while (index < length && status[index] >= '0' && status[index] <= '9');
        last = value;
        found = true;
    }
    if (!found || last == 0 || last > static_cast<unsigned long>(std::numeric_limits<pid_t>::max())) {
        return false;
    }
    tid = static_cast<pid_t>(last);
    return true;
}

struct LinuxDirectoryEntry64 {
    uint64_t inode;
    int64_t offset;
    uint16_t length;
    uint8_t type;
    char name[1];
};

bool parse_pid(const char* text, size_t capacity, pid_t& value) {
    unsigned long result{};
    size_t index{};
    for (; index < capacity && text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        const auto digit = static_cast<unsigned long>(text[index] - '0');
        if (result > (static_cast<unsigned long>(std::numeric_limits<pid_t>::max()) - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }
    if (index == 0 || index == capacity || result == 0) {
        return false;
    }
    value = static_cast<pid_t>(result);
    return true;
}

bool enumerate_threads(std::vector<ThreadIdentity>& threads) {
    threads.clear();
    const int descriptor = static_cast<int>(syscall(
        SYS_openat, AT_FDCWD, "/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0));
    if (descriptor < 0) {
        return false;
    }
    alignas(8) char entries[32768];
    bool success = true;
    for (;;) {
        const auto result = static_cast<ssize_t>(
            syscall(SYS_getdents64, descriptor, entries, sizeof(entries)));
        if (result == 0) {
            break;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            success = false;
            break;
        }
        for (size_t offset{}; offset < static_cast<size_t>(result);) {
            auto* entry = reinterpret_cast<LinuxDirectoryEntry64*>(entries + offset);
            constexpr size_t name_offset = offsetof(LinuxDirectoryEntry64, name);
            if (entry->length < name_offset + 1 ||
                entry->length > static_cast<size_t>(result) - offset) {
                success = false;
                break;
            }
            pid_t proc_tid{};
            if (parse_pid(entry->name, entry->length - name_offset, proc_tid)) {
                if (threads.size() == threads.capacity()) {
                    success = false;
                    break;
                }
                pid_t tid{};
                bool gone{};
                if (namespace_tid(proc_tid, tid, gone)) {
                    threads.push_back({tid, proc_tid});
                } else if (!gone) {
                    success = false;
                    break;
                }
            }
            offset += entry->length;
        }
        if (!success) {
            break;
        }
    }
    if (syscall(SYS_close, descriptor) != 0) {
        success = false;
    }
    std::sort(threads.begin(), threads.end(),
        [](const ThreadIdentity& left, const ThreadIdentity& right) { return left.tid < right.tid; });
    threads.erase(std::unique(threads.begin(), threads.end(),
        [](const ThreadIdentity& left, const ThreadIdentity& right) { return left.tid == right.tid; }), threads.end());
    return success;
}

bool signal_blocked(pid_t proc_tid, int signal_number) {
    char status[8192];
    size_t length{};
    bool gone{};
    if (!task_status(proc_tid, status, sizeof(status), length, gone)) {
        return !gone;
    }
    const auto value_offset = line_value(status, length, "SigBlk:", 7);
    if (!value_offset) {
        return true;
    }
    unsigned long long blocked{};
    bool found{};
    for (size_t index = *value_offset; index < length && status[index] != '\n'; ++index) {
        unsigned int digit{};
        if (status[index] >= '0' && status[index] <= '9') {
            digit = static_cast<unsigned int>(status[index] - '0');
        } else if (status[index] >= 'a' && status[index] <= 'f') {
            digit = static_cast<unsigned int>(status[index] - 'a') + 10;
        } else if (status[index] >= 'A' && status[index] <= 'F') {
            digit = static_cast<unsigned int>(status[index] - 'A') + 10;
        } else {
            continue;
        }
        if (blocked > (std::numeric_limits<unsigned long long>::max() - digit) / 16) {
            return true;
        }
        blocked = blocked * 16 + digit;
        found = true;
    }
    if (!found || signal_number <= 0 || signal_number > 64) {
        return true;
    }
    return (blocked & (1ULL << (signal_number - 1))) != 0;
}

bool wait_signal_unblocked(pid_t proc_tid, int signal_number) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (signal_blocked(proc_tid, signal_number)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

bool install_signal(int& signal_number, struct sigaction& previous) {
    for (int candidate = SIGRTMAX; candidate >= SIGRTMIN; --candidate) {
        struct sigaction current {};
        if (sigaction(candidate, nullptr, &current) != 0 || current.sa_handler != SIG_DFL) {
            continue;
        }
        struct sigaction action {};
        action.sa_sigaction = &rendezvous_signal;
        sigfillset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO | SA_RESTART;
        if (sigaction(candidate, &action, &previous) != 0) {
            continue;
        }
        if (previous.sa_handler == SIG_DFL) {
            signal_number = candidate;
            return true;
        }
        sigaction(candidate, &previous, nullptr);
    }
    return false;
}

ThreadSlot* live_slot(RendezvousState& state, pid_t tid) {
    const size_t count = state.count.load(std::memory_order_acquire);
    for (size_t index{}; index < count; ++index) {
        ThreadSlot& slot = state.slots[index];
        if (slot.tid == tid && slot.status.load(std::memory_order_acquire) != THREAD_GONE) {
            return &slot;
        }
    }
    return nullptr;
}

bool add_thread(RendezvousState& state, const ThreadIdentity& identity, pid_t current_tid) {
    if (identity.tid == current_tid || live_slot(state, identity.tid)) {
        return true;
    }
    if (!wait_signal_unblocked(identity.proc_tid, state.signal_number)) {
        return false;
    }
    const size_t index = state.count.load(std::memory_order_relaxed);
    if (index == RendezvousState::capacity) {
        return false;
    }
    ThreadSlot& slot = state.slots[index];
    slot.tid = identity.tid;
    slot.proc_tid = identity.proc_tid;
    slot.context.store(nullptr, std::memory_order_relaxed);
    slot.status.store(THREAD_PENDING, std::memory_order_relaxed);
    state.count.store(index + 1, std::memory_order_release);
    if (syscall(SYS_tgkill, getpid(), identity.tid, state.signal_number) == 0) {
        return true;
    }
    if (errno == ESRCH) {
        slot.status.store(THREAD_GONE, std::memory_order_release);
        return true;
    }
    return false;
}

bool wait_for_threads(RendezvousState& state) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        bool complete = true;
        const size_t count = state.count.load(std::memory_order_acquire);
        for (size_t index{}; index < count; ++index) {
            ThreadSlot& slot = state.slots[index];
            const uint8_t status = slot.status.load(std::memory_order_acquire);
            if (status == THREAD_PARKED || status == THREAD_DEPARTED || status == THREAD_GONE) {
                continue;
            }
            if (status == THREAD_PENDING && syscall(SYS_tgkill, getpid(), slot.tid, 0) != 0 && errno == ESRCH) {
                slot.status.store(THREAD_GONE, std::memory_order_release);
                continue;
            }
            complete = false;
        }
        if (complete) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
}

void release_threads(RendezvousState& state) {
    state.release.store(true, std::memory_order_release);
    for (;;) {
        bool complete = true;
        const size_t count = state.count.load(std::memory_order_acquire);
        for (size_t index{}; index < count; ++index) {
            const uint8_t status = state.slots[index].status.load(std::memory_order_acquire);
            if (status == THREAD_PARKED || status == THREAD_CLAIMED) {
                complete = false;
                break;
            }
        }
        if (complete) {
            return;
        }
        std::this_thread::yield();
    }
}

void settle_threads(RendezvousState& state) {
    const size_t count = state.count.load(std::memory_order_acquire);
    for (size_t index{}; index < count; ++index) {
        const ThreadSlot& slot = state.slots[index];
        if (slot.status.load(std::memory_order_acquire) == THREAD_DEPARTED) {
            wait_signal_unblocked(slot.proc_tid, state.signal_number);
        }
    }
}

bool address_in_range(uint8_t* address, const IpRange& range) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(range.address);
    return value >= begin && value - begin < range.size;
}

bool desired_ip(uint8_t* ip, std::span<const IpMapping> mappings,
    std::span<const IpRange> hazardous_ranges, uint8_t*& desired) {
    desired = ip;
    for (const auto& mapping : mappings) {
        if (mapping.from == ip) {
            desired = mapping.to;
            return true;
        }
    }
    return std::none_of(hazardous_ranges.begin(), hazardous_ranges.end(),
        [ip](const IpRange& range) { return address_in_range(ip, range); });
}

int protection(const VmAccess& access) {
    int result = 0;
    if (access.read) {
        result |= PROT_READ;
    }
    if (access.write) {
        result |= PROT_WRITE;
    }
    if (access.execute) {
        result |= PROT_EXEC;
    }
    return result;
}

bool add_pages(std::vector<ProtectedPage>& pages, uint8_t* address, size_t len, size_t page_size) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
    if (page_size == 0 || len > std::numeric_limits<uintptr_t>::max() - begin) {
        return false;
    }
    const uintptr_t value_end = begin + len;
    const uintptr_t remainder = value_end % page_size;
    if (remainder != 0 && page_size - remainder > std::numeric_limits<uintptr_t>::max() - value_end) {
        return false;
    }
    const uintptr_t start = begin - begin % page_size;
    const uintptr_t end = remainder == 0 ? value_end : value_end + page_size - remainder;
    for (uintptr_t page_value = start; page_value < end; page_value += page_size) {
        auto* page = reinterpret_cast<uint8_t*>(page_value);
        const bool present = std::any_of(pages.begin(), pages.end(), [page](const ProtectedPage& item) {
            return item.address == page;
        });
        if (present) {
            continue;
        }
        auto mapping = vm_query(page);
        if (!mapping || mapping->is_free || !mapping->access.execute) {
            return false;
        }
        pages.push_back({page, page_size, protection(mapping->access)});
    }
    return true;
}

}

std::expected<uint8_t*, OsError> vm_allocate(uint8_t* address, size_t size, VmAccess access) {
    int prot = 0;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;

    if (access == VM_ACCESS_R) {
        prot = PROT_READ;
    } else if (access == VM_ACCESS_RW) {
        prot = PROT_READ | PROT_WRITE;
    } else if (access == VM_ACCESS_RX) {
        prot = PROT_READ | PROT_EXEC;
    } else if (access == VM_ACCESS_RWX) {
        prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    } else {
        return std::unexpected{OsError::FAILED_TO_ALLOCATE};
    }

    auto* result = mmap(address, size, prot, flags, -1, 0);

    if (result == MAP_FAILED) {
        return std::unexpected{OsError::FAILED_TO_ALLOCATE};
    }

    return static_cast<uint8_t*>(result);
}

void vm_free(uint8_t* address, size_t size) {
    munmap(address, size);
}

std::expected<uint32_t, OsError> vm_protect(uint8_t* address, size_t size, VmAccess access) {
    int prot = 0;

    if (access == VM_ACCESS_R) {
        prot = PROT_READ;
    } else if (access == VM_ACCESS_RW) {
        prot = PROT_READ | PROT_WRITE;
    } else if (access == VM_ACCESS_RX) {
        prot = PROT_READ | PROT_EXEC;
    } else if (access == VM_ACCESS_RWX) {
        prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    } else {
        return std::unexpected{OsError::FAILED_TO_PROTECT};
    }

    return vm_protect(address, size, prot);
}

std::expected<uint32_t, OsError> vm_protect(uint8_t* address, size_t size, uint32_t protect) {
    auto mbi = vm_query(address);

    if (!mbi.has_value()) {
        return std::unexpected{OsError::FAILED_TO_PROTECT};
    }

    uint32_t old_protect = 0;

    if (mbi->access.read) {
        old_protect |= PROT_READ;
    }

    if (mbi->access.write) {
        old_protect |= PROT_WRITE;
    }

    if (mbi->access.execute) {
        old_protect |= PROT_EXEC;
    }

    auto* addr = align_down(address, static_cast<size_t>(sysconf(_SC_PAGESIZE)));

    size = size + static_cast<size_t>(address - addr);

    if (mprotect(addr, size, static_cast<int>(protect)) == -1) {
        return std::unexpected{OsError::FAILED_TO_PROTECT};
    }

    return old_protect;
}

std::expected<VmBasicInfo, OsError> vm_query(uint8_t* address) {
    auto* maps = fopen("/proc/self/maps", "r");

    if (maps == nullptr) {
        return std::unexpected{OsError::FAILED_TO_QUERY};
    }

    char line[512];
    unsigned long start;
    unsigned long end;
    char perms[5];
    unsigned long offset;
    unsigned int dev_major;
    unsigned int dev_minor;
    unsigned long inode;
    char path[256];
    unsigned long last_end =
        reinterpret_cast<unsigned long>(system_info().min_address); // Track the end address of the last mapping.
    auto addr = reinterpret_cast<unsigned long>(address);
    std::optional<VmBasicInfo> info = std::nullopt;

    while (fgets(line, sizeof(line), maps) != nullptr) {
        path[0] = '\0';

        if (sscanf(line, "%lx-%lx %4s %lx %x:%x %lu %255[^\n]", &start, &end, perms, &offset, &dev_major, &dev_minor,
                &inode, path) < 7) {
            continue;
        }

        if (last_end < start && addr >= last_end && addr < start) {
            info = std::make_optional<VmBasicInfo>(
                {reinterpret_cast<uint8_t*>(last_end), start - last_end, VmAccess{}, true});

            break;
        }

        last_end = end;

        if (addr >= start && addr < end) {
            info = std::make_optional<VmBasicInfo>({reinterpret_cast<uint8_t*>(start), end - start, VmAccess{}, false});

            if (perms[0] == 'r') {
                info->access.read = true;
            }

            if (perms[1] == 'w') {
                info->access.write = true;
            }

            if (perms[2] == 'x') {
                info->access.execute = true;
            }

            break;
        }
    }

    fclose(maps);

    if (!info.has_value()) {
        return std::unexpected{OsError::FAILED_TO_QUERY};
    }

    return info.value();
}

bool vm_is_readable(uint8_t* address, [[maybe_unused]] size_t size) {
    return vm_query(address).value_or(VmBasicInfo{}).access.read;
}

bool vm_is_writable(uint8_t* address, [[maybe_unused]] size_t size) {
    return vm_query(address).value_or(VmBasicInfo{}).access.write;
}

bool vm_is_executable(uint8_t* address) {
    return vm_query(address).value_or(VmBasicInfo{}).access.execute;
}

SystemInfo system_info() {
    auto page_size = static_cast<uint32_t>(sysconf(_SC_PAGESIZE));

    SystemInfo info{};
    info.page_size = page_size;
    info.allocation_granularity = page_size;
    info.min_address = reinterpret_cast<uint8_t*>(0x10000);
#if SAFETYHOOK_ARCH_X86_64
    info.max_address = reinterpret_cast<uint8_t*>(uintptr_t{1} << 47);
#elif SAFETYHOOK_ARCH_X86_32
    info.max_address = reinterpret_cast<uint8_t*>(std::numeric_limits<uintptr_t>::max());
#endif

    return info;
}

bool trap_threads(uint8_t* patch_address, size_t patch_size,
    std::span<const IpMapping> mappings, std::span<const IpRange> hazardous_ranges,
    const std::function<void()>& run_fn) {
    if (!patch_address || patch_size == 0 || !run_fn || g_poisoned.load(std::memory_order_acquire)) {
        return false;
    }

    std::scoped_lock lock{g_trap_mutex};
    if (g_poisoned.load(std::memory_order_acquire)) {
        return false;
    }

    auto state = std::make_unique<RendezvousState>();
    struct sigaction previous {};
    if (!install_signal(state->signal_number, previous)) {
        return false;
    }

    const pid_t current_tid = static_cast<pid_t>(syscall(SYS_gettid));
    std::vector<ThreadIdentity> threads;
    threads.reserve(RendezvousState::capacity);
    if (!enumerate_threads(threads)) {
        sigaction(state->signal_number, &previous, nullptr);
        return false;
    }
    for (const auto& identity : threads) {
        if (identity.tid != current_tid && !wait_signal_unblocked(identity.proc_tid, state->signal_number)) {
            sigaction(state->signal_number, &previous, nullptr);
            return false;
        }
    }

    const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    std::vector<ProtectedPage> pages;
    pages.reserve(2);
    if (!add_pages(pages, patch_address, patch_size, page_size)) {
        sigaction(state->signal_number, &previous, nullptr);
        return false;
    }

    std::vector<std::pair<void*, uint8_t*>> remapped;
    remapped.reserve(RendezvousState::capacity);

    g_rendezvous.store(state.get(), std::memory_order_release);
    bool safe = true;
    for (const auto& identity : threads) {
        if (!add_thread(*state, identity, current_tid)) {
            safe = false;
            break;
        }
    }

    for (size_t pass{}; safe && pass < 64; ++pass) {
        if (!wait_for_threads(*state) || !enumerate_threads(threads)) {
            safe = false;
            break;
        }
        bool added{};
        for (const auto& identity : threads) {
            if (identity.tid != current_tid && !live_slot(*state, identity.tid)) {
                if (!add_thread(*state, identity, current_tid)) {
                    safe = false;
                    break;
                }
                added = true;
            }
        }
        if (safe && !added) {
            break;
        }
        if (pass == 63) {
            safe = false;
        }
    }

    if (safe) {
        const size_t count = state->count.load(std::memory_order_acquire);
        for (size_t index{}; index < count; ++index) {
            ThreadSlot& slot = state->slots[index];
            if (slot.status.load(std::memory_order_acquire) != THREAD_PARKED) {
                continue;
            }
            void* context = slot.context.load(std::memory_order_acquire);
            uint8_t* destination{};
            if (!context || !desired_ip(instruction_pointer(context), mappings, hazardous_ranges, destination)) {
                safe = false;
                break;
            }
            if (destination != instruction_pointer(context)) {
                remapped.emplace_back(context, destination);
            }
        }
    }

    size_t protected_count{};
    if (safe) {
        for (; protected_count < pages.size(); ++protected_count) {
            const int writable = pages[protected_count].protection | PROT_READ | PROT_WRITE | PROT_EXEC;
            if (mprotect(pages[protected_count].address, pages[protected_count].size, writable) != 0) {
                safe = false;
                break;
            }
        }
    }
    if (!safe) {
        for (size_t index{}; index < protected_count; ++index) {
            mprotect(pages[index].address, pages[index].size, pages[index].protection);
        }
        state->release.store(true, std::memory_order_release);
        if (wait_for_threads(*state)) {
            release_threads(*state);
            settle_threads(*state);
            g_rendezvous.store(nullptr, std::memory_order_release);
            sigaction(state->signal_number, &previous, nullptr);
        } else {
            g_poisoned.store(true, std::memory_order_release);
            state.release();
        }
        return false;
    }

    run_fn();
    __builtin___clear_cache(
        reinterpret_cast<char*>(patch_address), reinterpret_cast<char*>(patch_address + patch_size));
    for (const auto& [context, destination] : remapped) {
        set_instruction_pointer(context, destination);
    }
    for (auto iterator = pages.rbegin(); iterator != pages.rend(); ++iterator) {
        if (mprotect(iterator->address, iterator->size, iterator->protection) != 0) {
            mprotect(iterator->address, iterator->size, PROT_READ | PROT_EXEC);
        }
    }

    release_threads(*state);
    settle_threads(*state);
    g_rendezvous.store(nullptr, std::memory_order_release);
    sigaction(state->signal_number, &previous, nullptr);
    return true;
}

void fix_ip(ThreadContext ctx, uint8_t* old_ip, uint8_t* new_ip) {
    if (ctx && instruction_pointer(ctx) == old_ip) {
        set_instruction_pointer(ctx, new_ip);
    }
}

} // namespace safetyhook

#endif
