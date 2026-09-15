#include <networkbasetypes.pb.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace
{
constexpr std::uint64_t kAllocation = UINT64_C(0x4b45454c504c4159);
struct alignas(std::max_align_t) Header { std::uint64_t marker; };
std::atomic<std::uint64_t> outstanding{};

void* Allocate(std::size_t size)
{
    if (size > SIZE_MAX - sizeof(Header)) throw std::bad_alloc();
    auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
    if (!header) throw std::bad_alloc();
    header->marker = kAllocation;
    ++outstanding;
    return header + 1;
}

void Release(void* pointer) noexcept
{
    if (!pointer) return;
    auto* header = static_cast<Header*>(pointer) - 1;
    if (header->marker != kAllocation) std::_Exit(197);
    header->marker = 0;
    --outstanding;
    std::free(header);
}
}

// This fixture models CS2's separate allocator. Allocations made by its copy
// of protobuf must return through this module, including long string buffers.
void* operator new(std::size_t size) { return Allocate(size); }
void* operator new[](std::size_t size) { return Allocate(size); }
void operator delete(void* pointer) noexcept { Release(pointer); }
void operator delete[](void* pointer) noexcept { Release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { Release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { Release(pointer); }

#if defined(_WIN32)
#define PLAYER_FIXTURE_EXPORT __declspec(dllexport)
#else
#define PLAYER_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" PLAYER_FIXTURE_EXPORT void KeelTest_FillPlayerInfo(
    google::protobuf::Message* message, const char* name)
{
    auto& info = *static_cast<CMsgPlayerInfo*>(message);
    info.set_name(name);
    info.set_fakeplayer(true);
    info.set_ishltv(false);
}

extern "C" PLAYER_FIXTURE_EXPORT std::uint64_t KeelTest_PlayerInfoAllocations()
{
    return outstanding.load();
}
