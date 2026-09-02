#ifndef KEELS2_SOURCE2_RUNTIME_HPP
#define KEELS2_SOURCE2_RUNTIME_HPP

#include <keels2/plugin.hpp>
#include <keels2/source2_runtime.h>

#include <cstdint>

namespace keels2::source2
{

class Runtime final
{
public:
    KeelResult Connect(const Context& context) noexcept
    {
        api_ = nullptr;
        context_.reset();
        const void* service{};
        const KeelResult result = context.QueryService(
            KEELS2_SOURCE2_RUNTIME_SERVICE_NAME,
            KEELS2_SOURCE2_RUNTIME_API_VERSION,
            &service);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        const auto* api = static_cast<const KeelSource2RuntimeApi*>(service);
        if (!api || api->size != sizeof(KeelSource2RuntimeApi) ||
            api->api_version != KEELS2_SOURCE2_RUNTIME_API_VERSION ||
            !api->server_command || !api->client_console_print ||
            !api->find_user_message)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        context_ = context.State();
        api_ = api;
        return KEEL_RESULT_OK;
    }

    explicit operator bool() const noexcept
    {
        return api_ && context_ &&
            context_->accepting_resources.load(std::memory_order_acquire);
    }

    KeelResult ServerCommand(const char* command) const noexcept
    {
        return *this
            ? api_->server_command(context_->plugin, command)
            : KEEL_RESULT_NOT_READY;
    }

    KeelResult ClientConsolePrint(std::int32_t slot, const char* message) const noexcept
    {
        return *this
            ? api_->client_console_print(context_->plugin, slot, message)
            : KEEL_RESULT_NOT_READY;
    }

    KeelResult FindUserMessage(const char* name, std::uint32_t& message_id) const noexcept
    {
        message_id = 0;
        return *this
            ? api_->find_user_message(context_->plugin, name, &message_id)
            : KEEL_RESULT_NOT_READY;
    }

private:
    std::shared_ptr<detail::ContextState> context_;
    const KeelSource2RuntimeApi* api_{};
};

}

#endif
