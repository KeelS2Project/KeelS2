#include "source2_runtime_service.h"

#include "game_adapter.h"
#include "host.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace keels2::host
{

Source2RuntimeService* Source2RuntimeService::active_{};

Source2RuntimeService::Source2RuntimeService(Host& host, GameAdapter& adapter)
    : host_(host), adapter_(adapter)
{
    if (active_)
    {
        throw std::runtime_error("Source 2 runtime service already exists");
    }
    active_ = this;
    api_ = {
        sizeof(KeelSource2RuntimeApi),
        KEELS2_SOURCE2_RUNTIME_API_VERSION,
        &ServerCommandEntry,
        &ClientConsolePrintEntry,
        &FindUserMessageEntry
    };
}

Source2RuntimeService::~Source2RuntimeService()
{
    active_ = nullptr;
}

const KeelSource2RuntimeApi& Source2RuntimeService::Api() const noexcept
{
    return api_;
}

KeelResult Source2RuntimeService::ServerCommandEntry(
    KeelPluginHandle plugin,
    const char* command)
{
    try
    {
        return active_ ? active_->ServerCommand(plugin, command) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Source2RuntimeService::ClientConsolePrintEntry(
    KeelPluginHandle plugin,
    std::int32_t slot,
    const char* message)
{
    try
    {
        return active_
            ? active_->ClientConsolePrint(plugin, slot, message)
            : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult Source2RuntimeService::FindUserMessageEntry(
    KeelPluginHandle plugin,
    const char* name,
    std::uint32_t* message_id)
{
    try
    {
        return active_
            ? active_->FindUserMessage(plugin, name, message_id)
            : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        if (message_id)
        {
            *message_id = 0;
        }
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

bool Source2RuntimeService::ValidText(
    const char* text,
    std::size_t maximum,
    bool name) noexcept
{
    if (!text)
    {
        return false;
    }
    std::size_t length{};
    while (length <= maximum && text[length])
    {
        const unsigned char character = static_cast<unsigned char>(text[length]);
        if ((name && !(std::isalnum(character) || character == '_' || character == '.')) ||
            (!name && character < 0x20 && character != '\n' && character != '\t'))
        {
            return false;
        }
        ++length;
    }
    return length != 0 && length <= maximum;
}

bool Source2RuntimeService::Ready(KeelPluginHandle plugin) const noexcept
{
    const PluginRecord* owner = host_.PluginByHandle(plugin);
    return host_.accepting_resources_ && owner && owner->accepting_resources &&
        owner->state == PluginState::loaded && !owner->transitioning;
}

KeelResult Source2RuntimeService::ServerCommand(
    KeelPluginHandle plugin,
    const char* command)
{
    if (!ValidText(command, 2048, false))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(host_.state_mutex_);
    if (!Ready(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    std::string error;
    return adapter_.ServerCommand(command, error);
}

KeelResult Source2RuntimeService::ClientConsolePrint(
    KeelPluginHandle plugin,
    std::int32_t slot,
    const char* message)
{
    if (slot < 0 || !ValidText(message, 4096, false))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(host_.state_mutex_);
    if (!Ready(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    std::string error;
    return adapter_.ClientConsolePrint(slot, message, error);
}

KeelResult Source2RuntimeService::FindUserMessage(
    KeelPluginHandle plugin,
    const char* name,
    std::uint32_t* message_id)
{
    if (!message_id || !ValidText(name, 127, true))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *message_id = 0;
    std::scoped_lock lock(host_.state_mutex_);
    if (!Ready(plugin))
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!adapter_.IsGameThread())
    {
        return KEEL_RESULT_WRONG_THREAD;
    }
    std::string error;
    return adapter_.FindUserMessage(name, *message_id, error);
}

}
