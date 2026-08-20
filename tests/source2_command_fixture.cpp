#include <tier1/convar.h>

#include <cstring>
#include <cstdlib>

IMemAlloc* g_pMemAlloc{};

int UtlVectorMemory_CalcNewAllocationCount(
    int allocation_count,
    int grow_size,
    int requested,
    int)
{
    if (grow_size > 0)
    {
        return ((requested + grow_size - 1) / grow_size) * grow_size;
    }
    int result = allocation_count > 0 ? allocation_count : 1;
    while (result < requested)
    {
        result *= 2;
    }
    return result;
}

void* UtlVectorMemory_Alloc(void* memory, bool reallocate, int new_size, int)
{
    if (new_size <= 0)
    {
        std::free(memory);
        return nullptr;
    }
    return reallocate ? std::realloc(memory, static_cast<std::size_t>(new_size))
                      : std::malloc(static_cast<std::size_t>(new_size));
}

void UtlVectorMemory_FailedAllocation(int, int)
{
    std::abort();
}

CCommand::CCommand()
{
    EnsureBuffers();
    Reset();
}

CCommand::CCommand(int argument_count, const char** arguments)
    : CCommand()
{
    char* argument_buffer = m_ArgvBuffer.Base();
    char* command_buffer = m_ArgSBuffer.Base();
    for (int index{}; index < argument_count; ++index)
    {
        m_Args.AddToTail(argument_buffer);
        const auto length = static_cast<int>(std::strlen(arguments[index]));
        std::memcpy(argument_buffer, arguments[index], static_cast<std::size_t>(length + 1));
        if (index == 0)
        {
            m_nArgv0Size = length;
        }
        argument_buffer += length + 1;

        const bool quoted = std::strchr(arguments[index], ' ') != nullptr;
        if (quoted)
        {
            *command_buffer++ = '"';
        }
        std::memcpy(command_buffer, arguments[index], static_cast<std::size_t>(length));
        command_buffer += length;
        if (quoted)
        {
            *command_buffer++ = '"';
        }
        if (index + 1 != argument_count)
        {
            *command_buffer++ = ' ';
        }
    }
    *command_buffer = '\0';
}

void CCommand::EnsureBuffers()
{
    m_ArgSBuffer.SetSize(MaxCommandLength());
    m_ArgvBuffer.SetSize(MaxCommandLength());
}

void CCommand::Reset()
{
    m_nArgv0Size = 0;
    m_ArgSBuffer.Base()[0] = '\0';
    m_Args.RemoveAll();
}
