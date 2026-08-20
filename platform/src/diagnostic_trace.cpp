#include <keels2/platform/diagnostic_trace.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace keels2::platform
{

namespace
{

constexpr std::size_t kMaximumTraceLine = 2048;

std::size_t AppendText(
    char (&line)[kMaximumTraceLine],
    std::size_t offset,
    std::string_view text) noexcept
{
    if (text.empty() || offset >= kMaximumTraceLine - 1)
    {
        return offset;
    }
    const std::size_t available = kMaximumTraceLine - 1 - offset;
    const std::size_t length = std::min(text.size(), available);
    std::memcpy(line + offset, text.data(), length);
    return offset + length;
}

}

void AppendShutdownTrace(std::string_view event, std::string_view detail) noexcept
{
    if (event.empty())
    {
        return;
    }

    char line[kMaximumTraceLine]{};
    std::size_t length = AppendText(line, 0, event);
    if (!detail.empty())
    {
        length = AppendText(line, length, ": ");
        length = AppendText(line, length, detail);
    }
    line[length++] = '\n';

#if defined(_WIN32)
    const wchar_t* path = _wgetenv(L"KEELS2_SHUTDOWN_TRACE_FILE");
    if (!path || path[0] == L'\0')
    {
        return;
    }
    const HANDLE file = CreateFileW(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD written{};
    static_cast<void>(WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr));
    static_cast<void>(FlushFileBuffers(file));
    CloseHandle(file);
#else
    const char* path = std::getenv("KEELS2_SHUTDOWN_TRACE_FILE");
    if (!path || path[0] == '\0')
    {
        return;
    }
    const int file = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (file < 0)
    {
        return;
    }
    std::size_t offset{};
    while (offset < length)
    {
        const ssize_t written = write(file, line + offset, length - offset);
        if (written <= 0)
        {
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    static_cast<void>(fsync(file));
    close(file);
#endif
}

}
