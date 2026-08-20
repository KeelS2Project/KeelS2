#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <limits>
#include <string>

#if defined(_WIN32)
#define KEELS2_FAKE_TIER0_EXPORT __declspec(dllexport)
#else
#define KEELS2_FAKE_TIER0_EXPORT __attribute__((visibility("default")))
#endif

namespace
{

std::string g_message;
std::string g_messages;

}

class CBufferString
{
public:
    KEELS2_FAKE_TIER0_EXPORT const char* Insert(
        int index,
        const char* source,
        int count = -1,
        bool ignore_alignment = false);
    KEELS2_FAKE_TIER0_EXPORT void Purge(int allocated_bytes_to_preserve = 0);
    KEELS2_FAKE_TIER0_EXPORT int Format(const char* format, ...);
    KEELS2_FAKE_TIER0_EXPORT const char* AppendFormat(const char* format, ...);

private:
    int length_{};
    int allocated_size_{};
    union
    {
        char* pointer_;
        char local_[8];
    } storage_{};
};

class CUtlString
{
public:
    KEELS2_FAKE_TIER0_EXPORT void Set(const char* value);
    KEELS2_FAKE_TIER0_EXPORT void SetDirect(const char* value, int length);
    KEELS2_FAKE_TIER0_EXPORT void Purge();
    KEELS2_FAKE_TIER0_EXPORT void Trim(const char* trim_characters);
    KEELS2_FAKE_TIER0_EXPORT bool operator==(const CUtlString& other) const;

private:
    char* value_{};
};

struct characterset_t;

class CUtlBuffer
{
public:
    enum BufferFlags_t
    {
        BF_NONE = 0
    };

    KEELS2_FAKE_TIER0_EXPORT CUtlBuffer(
        const void* buffer,
        int size,
        BufferFlags_t flags = BF_NONE);
    KEELS2_FAKE_TIER0_EXPORT int ParseToken(
        const characterset_t* breaks,
        char* token,
        int maximum_length,
        bool parse_comments = true);
};

class IMemAlloc;

KEELS2_FAKE_TIER0_EXPORT IMemAlloc* g_pMemAlloc{};

const char* CBufferString::Insert(
    int index,
    const char* source,
    int count,
    bool ignore_alignment)
{
    static_cast<void>(ignore_alignment);
    constexpr int length_mask = (1 << 30) - 1;
    constexpr int overflow = 1 << 30;
    constexpr int stack = 1 << 30;
    const int length = length_ & length_mask;
    const int capacity = allocated_size_ & length_mask;
    char* destination = (allocated_size_ & stack) != 0 ? storage_.local_ : storage_.pointer_;
    if (!destination || !source || index < 0 || index > length)
    {
        return destination ? destination : "";
    }
    const int source_length = count < 0
        ? static_cast<int>(std::strlen(source))
        : count;
    if (source_length < 0 || source_length >= capacity - length)
    {
        length_ |= overflow;
        return destination;
    }
    std::memmove(
        destination + index + source_length,
        destination + index,
        static_cast<std::size_t>(length - index + 1));
    std::memcpy(destination + index, source, static_cast<std::size_t>(source_length));
    length_ = (length_ & ~length_mask) | (length + source_length);
    return destination;
}

void CBufferString::Purge(int allocated_bytes_to_preserve)
{
    constexpr int free_heap = static_cast<int>(1u << 31);
    constexpr int stack = 1 << 30;
    constexpr int allow_heap = static_cast<int>(1u << 31);
    if ((length_ & free_heap) != 0 && (allocated_size_ & stack) == 0)
    {
        std::free(storage_.pointer_);
    }
    const int allow = allocated_size_ & allow_heap;
    length_ = 0;
        allocated_size_ = allocated_bytes_to_preserve > 0
            ? allow | stack | (allocated_bytes_to_preserve + 8)
        : 0;
    storage_.pointer_ = nullptr;
}

int CBufferString::Format(const char* format, ...)
{
    char buffer[1024]{};
    std::va_list arguments;
    va_start(arguments, format);
    const int length = format
        ? std::vsnprintf(buffer, sizeof(buffer), format, arguments)
        : -1;
    va_end(arguments);
    constexpr int length_mask = (1 << 30) - 1;
    length_ &= ~length_mask;
    char* destination = (allocated_size_ & (1 << 30)) != 0
        ? storage_.local_
        : storage_.pointer_;
    if (destination)
    {
        destination[0] = '\0';
    }
    if (length < 0)
    {
        return length;
    }
    Insert(0, buffer, std::min(length, static_cast<int>(sizeof(buffer) - 1)));
    return length;
}

const char* CBufferString::AppendFormat(const char* format, ...)
{
    char buffer[1024]{};
    std::va_list arguments;
    va_start(arguments, format);
    const int length = format
        ? std::vsnprintf(buffer, sizeof(buffer), format, arguments)
        : -1;
    va_end(arguments);
    constexpr int length_mask = (1 << 30) - 1;
    if (length >= 0)
    {
        Insert(
            length_ & length_mask,
            buffer,
            std::min(length, static_cast<int>(sizeof(buffer) - 1)));
    }
    return (allocated_size_ & (1 << 30)) != 0
        ? storage_.local_
        : storage_.pointer_;
}

void CUtlString::Set(const char* value)
{
    SetDirect(value, value ? static_cast<int>(std::strlen(value)) : 0);
}

void CUtlString::SetDirect(const char* value, int length)
{
    Purge();
    if (!value || length <= 0)
    {
        return;
    }
    value_ = static_cast<char*>(std::malloc(static_cast<std::size_t>(length) + 1));
    if (!value_)
    {
        return;
    }
    std::memcpy(value_, value, static_cast<std::size_t>(length));
    value_[length] = '\0';
}

void CUtlString::Purge()
{
    std::free(value_);
    value_ = nullptr;
}

void CUtlString::Trim(const char* trim_characters)
{
    if (!value_ || !trim_characters)
    {
        return;
    }
    const std::size_t length = std::strlen(value_);
    std::size_t begin{};
    while (begin < length && std::strchr(trim_characters, value_[begin]))
    {
        ++begin;
    }
    std::size_t end = length;
    while (end > begin && std::strchr(trim_characters, value_[end - 1]))
    {
        --end;
    }
    if (begin != 0)
    {
        std::memmove(value_, value_ + begin, end - begin);
    }
    value_[end - begin] = '\0';
}

bool CUtlString::operator==(const CUtlString& other) const
{
    const char* left = value_ ? value_ : "";
    const char* right = other.value_ ? other.value_ : "";
    return std::strcmp(left, right) == 0;
}

CUtlBuffer::CUtlBuffer(const void* buffer, int size, BufferFlags_t flags)
{
    static_cast<void>(buffer);
    static_cast<void>(size);
    static_cast<void>(flags);
}

int CUtlBuffer::ParseToken(
    const characterset_t* breaks,
    char* token,
    int maximum_length,
    bool parse_comments)
{
    static_cast<void>(breaks);
    static_cast<void>(parse_comments);
    if (token && maximum_length > 0)
    {
        token[0] = '\0';
    }
    return 0;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT bool Plat_IsInDebugSession()
{
    return false;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void Plat_ExitProcess(int code)
{
    std::exit(code);
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void* UtlVectorMemory_Alloc(
    void* memory,
    bool reallocate,
    int new_size,
    int old_size)
{
    static_cast<void>(old_size);
    if (new_size <= 0)
    {
        if (reallocate)
        {
            std::free(memory);
        }
        return nullptr;
    }
    return reallocate
        ? std::realloc(memory, static_cast<std::size_t>(new_size))
        : std::malloc(static_cast<std::size_t>(new_size));
}

extern "C" KEELS2_FAKE_TIER0_EXPORT int UtlVectorMemory_CalcNewAllocationCount(
    int allocation_count,
    int grow_size,
    int new_size,
    int bytes_per_item)
{
    static_cast<void>(bytes_per_item);
    if (grow_size > 0)
    {
        return ((new_size + grow_size - 1) / grow_size) * grow_size;
    }
    int result = std::max(allocation_count, 1);
    while (result < new_size && result <= std::numeric_limits<int>::max() / 2)
    {
        result *= 2;
    }
    return std::max(result, new_size);
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void UtlVectorMemory_FailedAllocation(int, int)
{
}

extern "C" KEELS2_FAKE_TIER0_EXPORT int V_tier0_strlen(const char* value)
{
    return value ? static_cast<int>(std::strlen(value)) : 0;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT int V_stricmp_fast(
    const char* left,
    const char* right)
{
    if (!left || !right)
    {
        return left ? 1 : (right ? -1 : 0);
    }
    while (*left && *right)
    {
        const int difference = std::tolower(static_cast<unsigned char>(*left)) -
            std::tolower(static_cast<unsigned char>(*right));
        if (difference != 0)
        {
            return difference;
        }
        ++left;
        ++right;
    }
    return static_cast<unsigned char>(*left) - static_cast<unsigned char>(*right);
}

extern "C" KEELS2_FAKE_TIER0_EXPORT std::int64_t V_atoi(const char* value)
{
    return value ? std::strtoll(value, nullptr, 10) : 0;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT bool V_StringToBool(
    const char*,
    bool default_value,
    ...)
{
    return default_value;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT std::int16_t V_StringToInt16(
    const char*,
    std::int16_t default_value,
    ...)
{
    return default_value;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT std::uint16_t V_StringToUint16(
    const char*,
    std::uint16_t default_value,
    ...)
{
    return default_value;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT std::int32_t V_StringToInt32(
    const char*,
    std::int32_t default_value,
    ...)
{
    return default_value;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT std::uint32_t V_StringToUint32(
    const char*,
    std::uint32_t default_value,
    ...)
{
    return default_value;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT std::int64_t V_StringToInt64(
    const char*,
    std::int64_t default_value,
    ...)
{
    return default_value;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT std::uint64_t V_StringToUint64(
    const char*,
    std::uint64_t default_value,
    ...)
{
    return default_value;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT float V_StringToFloat32(
    const char*,
    float default_value,
    ...)
{
    return default_value;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT double V_StringToFloat64(
    const char*,
    double default_value,
    ...)
{
    return default_value;
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void V_StringToVector(const char*, ...)
{
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void V_StringToVector2D(const char*, ...)
{
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void V_StringToVector4D(const char*, ...)
{
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void V_StringToColor(const char*, ...)
{
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void V_StringToQAngle(const char*, ...)
{
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void V_StringToVectorWS(const char*, ...)
{
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void Msg(const char* format, ...)
{
    if (!format)
    {
        g_message.clear();
        return;
    }

    char buffer[1024]{};
    std::va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    if (length < 0)
    {
        g_message.clear();
        return;
    }

    g_message.assign(buffer);
    g_messages.append(g_message);
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void Warning(const char* format, ...)
{
    if (!format)
    {
        return;
    }
    char buffer[1024]{};
    std::va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length >= 0)
    {
        g_message.assign(buffer);
        g_messages.append(g_message);
    }
}

KEELS2_FAKE_TIER0_EXPORT void ConMsg(const char* format, ...)
{
    if (!format)
    {
        return;
    }
    char buffer[1024]{};
    std::va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length >= 0)
    {
        g_message.assign(buffer);
        g_messages.append(g_message);
    }
}

extern "C" KEELS2_FAKE_TIER0_EXPORT const char* KeelTest_LastMessage()
{
    return g_message.c_str();
}

extern "C" KEELS2_FAKE_TIER0_EXPORT const char* KeelTest_Messages()
{
    return g_messages.c_str();
}

extern "C" KEELS2_FAKE_TIER0_EXPORT void KeelTest_ClearMessages()
{
    g_message.clear();
    g_messages.clear();
}
