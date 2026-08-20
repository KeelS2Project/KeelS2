#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct MonitorResult
{
    DWORD process_id{};
    DWORD exit_code{};
    DWORD exception_code{};
    std::uintptr_t exception_address{};
    DWORD exception_thread{};
    DWORD monitor_error{};
    DWORD dump_error{};
    bool exception_observed{};
    bool dump_attempted{};
    bool dump_written{};
};

bool WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    return output.good();
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (size <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            size) != size)
    {
        return {};
    }
    return result;
}

std::string FormatResult(const MonitorResult& result)
{
    std::ostringstream output;
    output << "PROCESS_ID: " << result.process_id << '\n';
    output << "EXIT_CODE: 0x" << std::hex << std::setw(8) << std::setfill('0')
           << result.exit_code << '\n';
    if (result.exception_observed)
    {
        output << "UNHANDLED_EXCEPTION_CODE: 0x" << std::setw(8)
               << result.exception_code << '\n';
        output << "UNHANDLED_EXCEPTION_ADDRESS: 0x" << std::setw(sizeof(void*) * 2)
               << result.exception_address << '\n';
        output << std::dec << "UNHANDLED_EXCEPTION_THREAD: "
               << result.exception_thread << '\n';
    }
    else
    {
        output << "UNHANDLED_EXCEPTION: none\n";
    }
    if (result.dump_written)
    {
        output << "DUMP: written\n";
    }
    else if (result.dump_attempted)
    {
        output << "DUMP: failed (Win32 error " << std::dec << result.dump_error << ")\n";
    }
    else
    {
        output << "DUMP: not requested\n";
    }
    if (result.monitor_error != ERROR_SUCCESS)
    {
        output << "MONITOR_ERROR: " << std::dec << result.monitor_error << '\n';
    }
    else
    {
        output << "MONITOR: complete\n";
    }
    return output.str();
}

bool WriteDump(
    HANDLE process,
    DWORD process_id,
    const std::filesystem::path& dump_path,
    DWORD& error)
{
    const HANDLE dump = CreateFileW(
        dump_path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (dump == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    constexpr auto dump_type = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal |
        MiniDumpWithDataSegs |
        MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules |
        MiniDumpWithThreadInfo |
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpWithFullMemoryInfo);
    const bool written = MiniDumpWriteDump(
        process,
        process_id,
        dump,
        dump_type,
        nullptr,
        nullptr,
        nullptr) != FALSE;
    error = written ? ERROR_SUCCESS : GetLastError();
    static_cast<void>(FlushFileBuffers(dump));
    CloseHandle(dump);
    if (!written)
    {
        static_cast<void>(DeleteFileW(dump_path.c_str()));
    }
    return written;
}

void CloseDebugFile(HANDLE file)
{
    if (file && file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file);
    }
}

}

int wmain(int argument_count, wchar_t** arguments)
{
    if (argument_count != 7)
    {
        return 2;
    }

    const std::filesystem::path pid_path = arguments[1];
    const std::filesystem::path result_path = arguments[2];
    const std::filesystem::path dump_path = arguments[3];
    const std::filesystem::path working_directory = arguments[4];
    const std::filesystem::path executable = arguments[5];
    const std::filesystem::path argument_file = arguments[6];

    MonitorResult result;
    const std::string argument_text = ReadText(argument_file);
    std::wstring child_arguments = Utf8ToWide(argument_text);
    if (!argument_text.empty() && child_arguments.empty())
    {
        result.monitor_error = ERROR_NO_UNICODE_TRANSLATION;
        static_cast<void>(WriteText(result_path, FormatResult(result)));
        return 3;
    }

    std::wstring command_line = L"\"" + executable.wstring() + L"\"";
    if (!child_arguments.empty())
    {
        command_line += L" ";
        command_line += child_arguments;
    }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            executable.c_str(),
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_PROCESS_GROUP,
            nullptr,
            working_directory.c_str(),
            &startup,
            &process))
    {
        result.monitor_error = GetLastError();
        static_cast<void>(WriteText(result_path, FormatResult(result)));
        return 4;
    }

    result.process_id = process.dwProcessId;
    CloseHandle(process.hThread);
    if (!WriteText(pid_path, std::to_string(result.process_id) + "\n"))
    {
        result.monitor_error = ERROR_WRITE_FAULT;
    }
    else
    {
        static_cast<void>(DebugSetProcessKillOnExit(FALSE));
    }

    bool exited{};
    while (!exited && result.monitor_error == ERROR_SUCCESS)
    {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, INFINITE))
        {
            result.monitor_error = GetLastError();
            break;
        }

        DWORD continuation = DBG_CONTINUE;
        if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
        {
            const auto& exception = event.u.Exception;
            const DWORD code = exception.ExceptionRecord.ExceptionCode;
            const bool debugger_exception =
                code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP;
            continuation = debugger_exception ? DBG_CONTINUE : DBG_EXCEPTION_NOT_HANDLED;
            if (exception.dwFirstChance == 0 && !result.exception_observed)
            {
                result.exception_observed = true;
                result.exception_code = code;
                result.exception_address = reinterpret_cast<std::uintptr_t>(
                    exception.ExceptionRecord.ExceptionAddress);
                result.exception_thread = event.dwThreadId;
                result.dump_attempted = true;
                result.dump_written = WriteDump(
                    process.hProcess,
                    process.dwProcessId,
                    dump_path,
                    result.dump_error);
            }
        }
        else if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT)
        {
            CloseDebugFile(event.u.CreateProcessInfo.hFile);
        }
        else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT)
        {
            CloseDebugFile(event.u.LoadDll.hFile);
        }
        else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
        {
            result.exit_code = event.u.ExitProcess.dwExitCode;
            exited = true;
        }

        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continuation))
        {
            result.monitor_error = GetLastError();
            break;
        }
    }

    if (!exited && result.monitor_error == ERROR_SUCCESS)
    {
        result.monitor_error = ERROR_PROCESS_ABORTED;
    }
    static_cast<void>(WriteText(result_path, FormatResult(result)));
    CloseHandle(process.hProcess);
    if (result.monitor_error != ERROR_SUCCESS)
    {
        return 5;
    }
    if (result.dump_attempted && !result.dump_written)
    {
        return 6;
    }
    return 0;
}
