#include "source2_runtime_service.h"

#include "game_adapter.h"
#include "game_adapter_loader.h"
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
    native_api_.size = sizeof(native_api_);
    native_api_.api_version = KEELS2_NATIVE_RUNTIME_API_VERSION;
    native_api_.check_game_thread = &CheckGameThreadEntry;
    native_api_.client_console_print = &ConsoleEntry;
    native_api_.client_chat_print = &ChatEntry;
    native_api_.broadcast_chat = &BroadcastEntry;
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

const KeelNativeRuntimeApi& Source2RuntimeService::NativeApi() const noexcept
{
    return native_api_;
}

KeelResult Source2RuntimeService::CheckGameThreadEntry(KeelPluginHandle plugin)
{
    return NativeRequest(plugin, -1, nullptr, 0);
}

KeelResult Source2RuntimeService::ConsoleEntry(KeelPluginHandle plugin, std::int32_t slot, const char* text)
{
    return NativeRequest(plugin, slot, text, 1);
}

KeelResult Source2RuntimeService::ChatEntry(KeelPluginHandle plugin, std::int32_t slot, const char* text)
{
    return NativeRequest(plugin, slot, text, 2);
}

KeelResult Source2RuntimeService::BroadcastEntry(KeelPluginHandle plugin, const char* text)
{
    return NativeRequest(plugin, -1, text, 3);
}

KeelResult Source2RuntimeService::NativeRequest(KeelPluginHandle plugin, std::int32_t slot,
    const char* text, unsigned operation)
{
    if ((operation && !ValidText(text, operation == 1 ? 4096 : 512, false)) ||
        ((operation == 1 || operation == 2) && slot < 0))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        Host& host = Host::Instance();
        std::unique_lock lock(host.state_mutex_);
        PluginRecord* owner = host.PluginByHandle(plugin);
        const bool cleanup = owner && owner->unload_callback_active;
        const bool running = owner && host.accepting_resources_ && owner->accepting_resources &&
            !owner->cleanup_pending && (owner->loading || (owner->state == PluginState::loaded && !owner->transitioning));
        if (!host.adapter_ || !host.adapter_module_ || (!running && !(cleanup && operation == 0)))
        {
            return KEEL_RESULT_NOT_READY;
        }
        if (!host.adapter_->IsGameThread())
        {
            return KEEL_RESULT_WRONG_THREAD;
        }
        if (operation == 0)
        {
            return KEEL_RESULT_OK;
        }
        if (owner->active_native_operations == UINT32_MAX)
        {
            return KEEL_RESULT_BUSY;
        }
        ++owner->active_native_operations;
        struct Operation
        {
            std::uint32_t& active;
            std::unique_lock<std::recursive_mutex>& lock;
            ~Operation()
            {
                if (!lock.owns_lock())
                {
                    lock.lock();
                }
                --active;
            }
        } hold{owner->active_native_operations, lock};
        lock.unlock();
        if (operation == 1)
        {
            KeelPlayerInfo player{};
            const KeelResult found = host.adapter_module_->ReadPlayer(slot, player);
            if (found != KEEL_RESULT_OK)
            {
                return found;
            }
            if (player.user_id < 0 || (player.flags & KEELS2_PLAYER_SOURCE_TV))
            {
                return KEEL_RESULT_NOT_FOUND;
            }
            std::string error;
            return host.adapter_->ClientConsolePrint(slot, text, error);
        }
        return host.adapter_module_->PrintChat(slot, operation == 3 ? KEEL_TRUE : KEEL_FALSE, text);
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
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
