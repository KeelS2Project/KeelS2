#ifndef KEELS2_NATIVE_RUNTIME_HPP
#define KEELS2_NATIVE_RUNTIME_HPP

#include <keels2/native_runtime.h>
#include <keels2/plugin.hpp>
#include <playerslot.h>

namespace keels2::source2
{

class NativeRuntime final
{
public:
    KeelResult Connect(const Context& context) noexcept
    {
        api_ = nullptr;
        context_.reset();
        const void* service{};
        const KeelResult result = context.QueryService(
            KEELS2_NATIVE_RUNTIME_SERVICE_NAME, KEELS2_NATIVE_RUNTIME_API_VERSION, &service);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        const auto* api = static_cast<const KeelNativeRuntimeApi*>(service);
        if (!api || api->size != sizeof(*api) || api->api_version != KEELS2_NATIVE_RUNTIME_API_VERSION ||
            !api->check_game_thread || !api->client_console_print || !api->client_chat_print || !api->broadcast_chat)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        context_ = context.State();
        api_ = api;
        return KEEL_RESULT_OK;
    }

    explicit operator bool() const noexcept
    {
        return api_ && context_ && context_->native_access.load(std::memory_order_acquire);
    }

    KeelResult CheckGameThread() const noexcept
    {
        return *this ? api_->check_game_thread(context_->plugin) : KEEL_RESULT_NOT_READY;
    }

    KeelResult PrintToConsole(CPlayerSlot slot, const char* text) const noexcept
    {
        return *this ? api_->client_console_print(context_->plugin, slot.Get(), text) : KEEL_RESULT_NOT_READY;
    }

    KeelResult PrintToChat(CPlayerSlot slot, const char* text) const noexcept
    {
        return *this ? api_->client_chat_print(context_->plugin, slot.Get(), text) : KEEL_RESULT_NOT_READY;
    }

    KeelResult PrintToChatAll(const char* text) const noexcept
    {
        return *this ? api_->broadcast_chat(context_->plugin, text) : KEEL_RESULT_NOT_READY;
    }

private:
    const KeelNativeRuntimeApi* api_{};
    std::shared_ptr<detail::ContextState> context_;
};

}

#endif
