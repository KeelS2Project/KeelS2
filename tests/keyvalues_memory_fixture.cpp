#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_map>
#if defined(_WIN32)
#define KV_FIXTURE_EXPORT __declspec(dllexport)
using FixtureThreadId = unsigned int;
#else
#define KV_FIXTURE_EXPORT __attribute__((visibility("default")))
using FixtureThreadId = unsigned long long;
#endif
class IMemAlloc;
extern "C" IMemAlloc* g_pMemAlloc;
namespace {
struct Allocation { void* base; std::size_t size; std::size_t alignment; };
std::unordered_map<void*,Allocation> allocations;
std::array<void*,64> methods{};
struct Memory { void** methods; } memory{methods.data()};
[[noreturn]] void Unsupported() { std::abort(); }
void* Allocate(std::size_t size, std::size_t alignment = 16)
{
    if (!alignment || (alignment & (alignment-1)) || alignment > 4096 ||
        size > std::numeric_limits<std::size_t>::max()-alignment-32) throw std::bad_alloc();
    void* base = std::malloc(size+alignment+32);
    if (!base) throw std::bad_alloc();
    auto address = (reinterpret_cast<std::uintptr_t>(base)+32+alignment-1) & ~(alignment-1);
    void* value = reinterpret_cast<void*>(address);
    allocations.emplace(value,Allocation{base,size,alignment});
    return value;
}
void Free(void* value)
{
    if (!value) return;
    const auto found = allocations.find(value);
    if (found == allocations.end()) Unsupported();
    void* base = found->second.base;
    allocations.erase(found); std::free(base);
}
void* Reallocate(void* value, std::size_t size, std::size_t alignment)
{
    if (!value) return size ? Allocate(size,alignment) : nullptr;
    if (!size) { Free(value); return nullptr; }
    const auto found = allocations.find(value);
    if (found == allocations.end()) Unsupported();
    const auto old_size = found->second.size;
    void* result = Allocate(size,alignment);
    std::memcpy(result,value,std::min(size,old_size)); Free(value);
    return result;
}
void* AllocSlot(void*, std::size_t size) { return Allocate(size); }
void* ReallocSlot(void*, void* value, std::size_t size) { return Reallocate(value,size,16); }
void FreeSlot(void*, void* value) { Free(value); }
void* AlignedSlot(void*, std::size_t size, std::size_t alignment) { return Allocate(size,alignment); }
void* ReallocAlignedSlot(void*, void* value, std::size_t size, std::size_t alignment) { return Reallocate(value,size,alignment); }
void* RegionSlot(void*, unsigned char, std::size_t size) { return Allocate(size); }
void RegionFreeSlot(void*, unsigned char, void* value) { Free(value); }
void* RegionAlignedSlot(void*, unsigned char, std::size_t size, std::size_t alignment) { return Allocate(size,alignment); }
std::size_t SizeSlot(void*, void* value) {
    const auto found = allocations.find(value);
    if (found == allocations.end()) Unsupported();
    return found->second.size;
}
template<class T> void* Address(T value) {
    static_assert(sizeof(value) == sizeof(void*)); void* result{}; std::memcpy(&result,&value,sizeof(result)); return result;
}
}
extern "C" KV_FIXTURE_EXPORT void KeelFixtureKeyValuesMemoryStart()
{
    if (g_pMemAlloc || !allocations.empty()) Unsupported();
    methods.fill(Address(&Unsupported));
#if defined(_WIN32)
    constexpr unsigned first = 1;
#else
    constexpr unsigned first = 2;
#endif
    methods[first] = methods[first+6] = Address(&AllocSlot);
    methods[first+1] = methods[first+7] = Address(&ReallocSlot);
    methods[first+2] = methods[first+8] = methods[first+5] = methods[first+11] = Address(&FreeSlot);
    methods[first+3] = methods[first+9] = Address(&AlignedSlot);
    methods[first+4] = methods[first+10] = Address(&ReallocAlignedSlot);
    methods[first+12] = Address(&RegionSlot); methods[first+13] = methods[first+15] = Address(&RegionFreeSlot);
    methods[first+14] = Address(&RegionAlignedSlot);
    methods[first+16] = methods[first+17] = Address(&SizeSlot);
    g_pMemAlloc = reinterpret_cast<IMemAlloc*>(&memory);
}
extern "C" KV_FIXTURE_EXPORT std::size_t KeelFixtureKeyValuesMemoryCount() { return allocations.size(); }
extern "C" KV_FIXTURE_EXPORT bool KeelFixtureKeyValuesMemoryOwns(void* value) { return allocations.contains(value); }
extern "C" KV_FIXTURE_EXPORT void KeelFixtureKeyValuesMemoryStop() {
    if (!allocations.empty()) Unsupported();
    g_pMemAlloc = nullptr;
}
void* KeyValuesFixtureVectorAlloc(void* value, bool reallocate, std::size_t size) {
    return reallocate ? Reallocate(value,size,16) : Allocate(size);
}
bool KeyValuesFixtureMemoryActive() { return g_pMemAlloc == reinterpret_cast<IMemAlloc*>(&memory); }
extern "C" {
KV_FIXTURE_EXPORT int LOG_GENERAL = 0;
KV_FIXTURE_EXPORT bool LoggingSystem_IsChannelEnabled(int, int) { return false; }
KV_FIXTURE_EXPORT int LoggingSystem_Log(int, int, const char*, ...) { Unsupported(); }
KV_FIXTURE_EXPORT void* V_tier0_memmove(void* dest, const void* source, std::size_t count) { return std::memmove(dest,source,count); }
KV_FIXTURE_EXPORT int _V_strnicmp_fast(const char* left, const char* right, int length) {
    const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c+('a'-'A') : c; };
    for (int i = 0; i < length; ++i) {
        const int a = lower(static_cast<unsigned char>(left[i])), b = lower(static_cast<unsigned char>(right[i]));
        if (a != b || !left[i] || !right[i]) return a-b;
    }
    return 0;
}
KV_FIXTURE_EXPORT void _V_strncpy(char* dest, const char* source, int size) {
    if (size <= 0) return;
    const auto count = std::min(std::strlen(source),static_cast<std::size_t>(size-1));
    std::memcpy(dest,source,count); dest[count] = 0;
}
KV_FIXTURE_EXPORT bool V_CompareNameWithWildcards(const char*, const char*) { Unsupported(); }
KV_FIXTURE_EXPORT FixtureThreadId ThreadGetCurrentId() { return 1; }
KV_FIXTURE_EXPORT void Plat_NonFatalErrorFunc(const char*, ...) { Unsupported(); }
}
class CAtomicMutex { public: KV_FIXTURE_EXPORT void AcquireLock(FixtureThreadId); };
void CAtomicMutex::AcquireLock(FixtureThreadId) { Unsupported(); }
KV_FIXTURE_EXPORT void ThreadAtomicNotifyOne(const unsigned int*) { Unsupported(); }
class CSplitString { KV_FIXTURE_EXPORT void Split(const char*,int,const char**,int,bool); };
void CSplitString::Split(const char*,int,const char**,int,bool) { Unsupported(); }
