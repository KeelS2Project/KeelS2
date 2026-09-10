#ifndef KEELS2_FACTORIES_HPP
#define KEELS2_FACTORIES_HPP

#include <keels2/factories.h>
#include <keels2/source2.hpp>

namespace keels2::factories
{

class Service final
{
public:
    KeelResult Connect(const Context& context) noexcept
    {
        api_ = nullptr;
        context_ = context.State();
        const void* value{};
        const auto result = context.QueryService(
            KEELS2_FACTORIES_SERVICE_NAME, KEELS2_FACTORIES_API_VERSION, &value);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        const auto* api = static_cast<const KeelFactoriesApi*>(value);
        if (!api || api->size != sizeof(*api) ||
            api->api_version != KEELS2_FACTORIES_API_VERSION ||
            !api->subscribe || !api->unsubscribe || !api->query_original)
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

    KeelResult Subscribe(source2::Factory factory, const char* name,
        KeelFactoryCallback callback, void* data, std::int32_t priority,
        KeelFactorySubscriptionHandle& subscription, std::uint32_t flags = 0) const noexcept
    {
        subscription = 0;
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        const KeelFactorySubscriptionSpec spec{sizeof(spec),
            static_cast<KeelSource2Factory>(factory), name, priority, flags, callback, data};
        return api_->subscribe(context_->plugin, &spec, &subscription);
    }

    KeelResult Unsubscribe(KeelFactorySubscriptionHandle subscription) const noexcept
    {
        return *this ? api_->unsubscribe(context_->plugin, subscription) : KEEL_RESULT_NOT_READY;
    }

    KeelResult Original(source2::Factory factory, const char* name,
        KeelFactoryResult& result) const noexcept
    {
        result = {sizeof(result), 1, nullptr};
        return *this ? api_->query_original(context_->plugin,
            static_cast<KeelSource2Factory>(factory), name, &result) : KEEL_RESULT_NOT_READY;
    }

private:
    std::shared_ptr<detail::ContextState> context_;
    const KeelFactoriesApi* api_{};
};

}

#endif
