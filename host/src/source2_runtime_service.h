#ifndef KEELS2_HOST_SOURCE2_RUNTIME_SERVICE_H
#define KEELS2_HOST_SOURCE2_RUNTIME_SERVICE_H

#include <keels2/source2_runtime.h>

#include <cstddef>
#include <cstdint>

namespace keels2::host
{

class GameAdapter;
class Host;

class Source2RuntimeService final
{
public:
    Source2RuntimeService(Host& host, GameAdapter& adapter);
    ~Source2RuntimeService();
    Source2RuntimeService(const Source2RuntimeService&) = delete;
    Source2RuntimeService& operator=(const Source2RuntimeService&) = delete;

    const KeelSource2RuntimeApi& Api() const noexcept;

private:
    static KeelResult ServerCommandEntry(KeelPluginHandle plugin, const char* command);
    static KeelResult ClientConsolePrintEntry(
        KeelPluginHandle plugin,
        std::int32_t slot,
        const char* message);
    static KeelResult FindUserMessageEntry(
        KeelPluginHandle plugin,
        const char* name,
        std::uint32_t* message_id);

    KeelResult ServerCommand(KeelPluginHandle plugin, const char* command);
    KeelResult ClientConsolePrint(
        KeelPluginHandle plugin,
        std::int32_t slot,
        const char* message);
    KeelResult FindUserMessage(
        KeelPluginHandle plugin,
        const char* name,
        std::uint32_t* message_id);
    static bool ValidText(const char* text, std::size_t maximum, bool name) noexcept;
    bool Ready(KeelPluginHandle plugin) const noexcept;

    Host& host_;
    GameAdapter& adapter_;
    KeelSource2RuntimeApi api_{};

    static Source2RuntimeService* active_;
};

}

#endif
