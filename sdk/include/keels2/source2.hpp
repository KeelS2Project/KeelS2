#ifndef KEELS2_SOURCE2_HPP
#define KEELS2_SOURCE2_HPP

#include <keels2/plugin.hpp>
#include <keels2/source2.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <utility>

namespace keels2::source2
{

enum class Capability : KeelSource2Capability
{
    named = KEELS2_SOURCE2_CAPABILITY_NAMED,
    server = KEELS2_SOURCE2_CAPABILITY_SERVER,
    game_clients = KEELS2_SOURCE2_CAPABILITY_GAME_CLIENTS,
    cvar = KEELS2_SOURCE2_CAPABILITY_CVAR
};

enum class Factory : KeelSource2Factory
{
    engine = KEELS2_SOURCE2_FACTORY_ENGINE,
    server = KEELS2_SOURCE2_FACTORY_SERVER
};

enum class Ownership : KeelSource2Ownership
{
    borrowed = KEELS2_SOURCE2_OWNERSHIP_BORROWED
};

enum class Lifetime : KeelSource2Lifetime
{
    host = KEELS2_SOURCE2_LIFETIME_HOST
};

class Interface final
{
public:
    Interface() = default;

    explicit operator bool() const noexcept
    {
        return info_.instance && context_ &&
            context_->accepting_resources.load(std::memory_order_acquire) &&
            context_->api;
    }

    void Reset() noexcept
    {
        context_.reset();
        info_ = {};
    }

    Capability Type() const noexcept
    {
        return static_cast<Capability>(info_.capability);
    }

    Factory Origin() const noexcept
    {
        return static_cast<Factory>(info_.factory);
    }

    Ownership Owner() const noexcept
    {
        return static_cast<Ownership>(info_.ownership);
    }

    Lifetime ValidUntil() const noexcept
    {
        return static_cast<Lifetime>(info_.lifetime);
    }

    void* Raw() const noexcept
    {
        return *this ? info_.instance : nullptr;
    }

    template <typename Type>
    Type* Get() const noexcept
    {
        return static_cast<Type*>(Raw());
    }

    const char* Name() const noexcept
    {
        return *this ? info_.interface_name : nullptr;
    }

    const char* Module() const noexcept
    {
        return *this ? info_.module_name : nullptr;
    }

    const char* ModulePath() const noexcept
    {
        return *this ? info_.module_path : nullptr;
    }

    const char* CompatibilityProfile() const noexcept
    {
        return *this ? info_.compatibility_profile : nullptr;
    }

private:
    friend class Service;

    void Adopt(
        std::shared_ptr<detail::ContextState> context,
        const KeelSource2InterfaceInfo& info) noexcept
    {
        context_ = std::move(context);
        info_ = info;
    }

    std::shared_ptr<detail::ContextState> context_;
    KeelSource2InterfaceInfo info_{};
};

class Service final
{
public:
    KeelResult Connect(const Context& context) noexcept
    {
        api_ = nullptr;
        context_.reset();
        const void* service{};
        const KeelResult result = context.QueryService(
            KEELS2_SOURCE2_SERVICE_NAME,
            KEELS2_SOURCE2_API_VERSION,
            &service);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        const auto* api = static_cast<const KeelSource2Api*>(service);
        if (!api || api->size != sizeof(KeelSource2Api) ||
            api->api_version != KEELS2_SOURCE2_API_VERSION || !api->query_interface ||
            !api->query_named_interface)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        context_ = context.State();
        api_ = api;
        return KEEL_RESULT_OK;
    }

    explicit operator bool() const noexcept
    {
        return context_ &&
            context_->accepting_resources.load(std::memory_order_acquire) &&
            context_->api && api_;
    }

    KeelResult Query(Capability capability, Interface& output) const noexcept
    {
        output.Reset();
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        KeelSource2InterfaceInfo info{};
        info.size = sizeof(info);
        const KeelResult result = api_->query_interface(
            context_->plugin,
            static_cast<KeelSource2Capability>(capability),
            &info);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        if (!ValidInfo(info) ||
            info.capability != static_cast<KeelSource2Capability>(capability) ||
            (info.factory != KEELS2_SOURCE2_FACTORY_ENGINE &&
                info.factory != KEELS2_SOURCE2_FACTORY_SERVER))
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        output.Adopt(context_, info);
        return KEEL_RESULT_OK;
    }

    KeelResult Query(
        Factory factory,
        const char* interface_name,
        Interface& output) const noexcept
    {
        output.Reset();
        if (!interface_name || !interface_name[0])
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        KeelSource2InterfaceInfo info{};
        info.size = sizeof(info);
        const KeelResult result = api_->query_named_interface(
            context_->plugin,
            static_cast<KeelSource2Factory>(factory),
            interface_name,
            &info);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        if (!ValidInfo(info) ||
            info.capability != KEELS2_SOURCE2_CAPABILITY_NAMED ||
            info.factory != static_cast<KeelSource2Factory>(factory) ||
            std::strcmp(info.interface_name, interface_name) != 0)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        output.Adopt(context_, info);
        return KEEL_RESULT_OK;
    }

private:
    static bool ValidText(const char* text) noexcept
    {
        return text && text[0];
    }

    static bool ValidInfo(const KeelSource2InterfaceInfo& info) noexcept
    {
        return info.size == sizeof(info) &&
            info.ownership == KEELS2_SOURCE2_OWNERSHIP_BORROWED &&
            info.lifetime == KEELS2_SOURCE2_LIFETIME_HOST && info.reserved == 0 &&
            info.instance && ValidText(info.interface_name) &&
            ValidText(info.module_name) && ValidText(info.module_path) &&
            ValidText(info.compatibility_profile);
    }

    std::shared_ptr<detail::ContextState> context_;
    const KeelSource2Api* api_{};
};

}

#endif
