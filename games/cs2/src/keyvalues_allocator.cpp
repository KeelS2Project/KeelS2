#include <entity2/entitysystem.h>
#include <tier0/memalloc.h>
#include <new>
#include <tier0/memdbgoff.h>

// This module uses the SDK's normal, privately owned keyvalue arenas. It never
// borrows the game entity system's map allocator or resolves entity names.
CGameEntitySystem* GameEntitySystem()
{
    return nullptr;
}

void* operator new(std::size_t size)
{
    void* result = g_pMemAlloc ? MemAlloc_Alloc(size ? size : 1) : nullptr;

    if (!result)
        throw std::bad_alloc();

    return result;
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* value) noexcept
{
    if (value)
        MemAlloc_Free(value);
}

void operator delete[](void* value) noexcept
{
    ::operator delete(value);
}

void operator delete(void* value, std::size_t) noexcept
{
    ::operator delete(value);
}

void operator delete[](void* value, std::size_t) noexcept
{
    ::operator delete(value);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
    return ::operator new(size, tag);
}

void operator delete(void* value, const std::nothrow_t&) noexcept
{
    ::operator delete(value);
}

void operator delete[](void* value, const std::nothrow_t&) noexcept
{
    ::operator delete(value);
}

void* operator new(std::size_t size, int, const char*, int)
{
    return ::operator new(size);
}

void* operator new[](std::size_t size, int, const char*, int)
{
    return ::operator new(size);
}

void operator delete(void* value, int, const char*, int) noexcept
{
    ::operator delete(value);
}

void operator delete[](void* value, int, const char*, int) noexcept
{
    ::operator delete(value);
}
