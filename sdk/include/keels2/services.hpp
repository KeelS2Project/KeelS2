#ifndef KEELS2_SERVICES_HPP
#define KEELS2_SERVICES_HPP

#include <keels2/plugin.hpp>
#include <keels2/services.h>

#include <cstdint>

namespace keels2::services
{

class Service final
{
public:
    KeelResult Connect(const Context& context) noexcept
    {
        api_ = nullptr;
        context_ = context.State();
        const void* value{};
        const KeelResult result = context.QueryService(
            KEELS2_SERVICES_SERVICE_NAME,
            KEELS2_SERVICES_API_VERSION,
            &value);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        const auto* api = static_cast<const KeelServicesApi*>(value);
        if (!api || api->size != sizeof(KeelServicesApi) ||
            api->api_version != KEELS2_SERVICES_API_VERSION || !api->publish ||
            !api->withdraw || !api->release)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        api_ = api;
        return KEEL_RESULT_OK;
    }

    explicit operator bool() const noexcept
    {
        return api_ && context_ &&
            context_->accepting_resources.load(std::memory_order_acquire);
    }

    KeelResult Publish(
        const char* name,
        std::uint32_t version,
        const void* value,
        KeelServiceHandle& publication) const noexcept
    {
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        const KeelServiceSpec spec{sizeof(KeelServiceSpec), version, name, value};
        return api_->publish(context_->plugin, &spec, &publication);
    }

    KeelResult Withdraw(KeelServiceHandle publication) const noexcept
    {
        return *this
            ? api_->withdraw(context_->plugin, publication)
            : KEEL_RESULT_NOT_READY;
    }

    KeelResult Release(const char* name, std::uint32_t version) const noexcept
    {
        return *this
            ? api_->release(context_->plugin, name, version)
            : KEEL_RESULT_NOT_READY;
    }

private:
    std::shared_ptr<detail::ContextState> context_;
    const KeelServicesApi* api_{};
};

}

#endif
