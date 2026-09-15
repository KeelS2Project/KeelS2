#include <keels2/platform/console.h>
#include <keels2/platform/dynamic_library.h>

#include <cstring>
#include <filesystem>
#include <string>

int main(int argument_count, char** arguments)
{
    if (argument_count != 2)
    {
        return 1;
    }

    keels2::platform::DynamicLibrary tier0;
    std::string error;
    if (!tier0.Open(std::filesystem::path(arguments[1]), error))
    {
        return 2;
    }

    using LastMessageFunction = const char* (*)();
    const auto last_message = reinterpret_cast<LastMessageFunction>(tier0.Symbol("KeelTest_LastMessage"));
    if (!last_message)
    {
        return 3;
    }

    keels2::platform::WriteEngineConsole("engine console test\n");
    const char* message = last_message();
    if (!message || std::strcmp(message, "engine console test\n") != 0)
    {
        return 4;
    }

    return 0;
}
