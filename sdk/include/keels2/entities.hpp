#ifndef KEELS2_ENTITIES_HPP
#define KEELS2_ENTITIES_HPP

#include <keels2/entities.h>
#include <keels2/plugin.hpp>
#include <keels2/schema.hpp>

#include <atomic>
#include <memory>
#include <utility>

namespace keels2::entities
{

class Entity final
{
public:
    Entity() = default;

    ~Entity()
    {
        static_cast<void>(Reset());
    }

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    Entity(Entity&& other) noexcept
    {
        MoveFrom(other);
    }

    Entity& operator=(Entity&& other) noexcept
    {
        if (this != &other)
        {
            static_cast<void>(Reset());
            MoveFrom(other);
        }
        return *this;
    }

    explicit operator bool() const noexcept
    {
        return Valid();
    }

    bool Valid() const noexcept
    {
        if (!LocallyValid() || !api_->describe)
        {
            return false;
        }
        KeelEntityInfo info{};
        info.size = sizeof(info);
        return api_->describe(context_->plugin, handle_, &info) == KEEL_RESULT_OK &&
            info.size == sizeof(info) && info.reserved == 0 &&
            info.index == info_.index && info.source2_handle == info_.source2_handle &&
            info.epoch == info_.epoch;
    }

    KeelResult Reset() noexcept
    {
        if (!handle_)
        {
            return KEEL_RESULT_OK;
        }
        if (!context_ ||
            !context_->accepting_resources.load(std::memory_order_acquire) ||
            !api_ || !api_->release)
        {
            Clear();
            return KEEL_RESULT_NOT_READY;
        }
        const KeelResult result = api_->release(context_->plugin, handle_);
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            Clear();
        }
        return result;
    }

    int Index() const noexcept
    {
        return LocallyValid() ? info_.index : -1;
    }

    uint32 Source2Handle() const noexcept
    {
        return LocallyValid()
            ? info_.source2_handle
            : KEELS2_INVALID_SOURCE2_ENTITY_HANDLE;
    }

    template <typename Value>
    bool Read(const schema::Field<Value>& field, Value& value) const noexcept
    {
        static_assert(schema::detail::kSupportedValue<Value>);
        value = Value{};
        return LocallyValid() && field.RawHandle() && field.context_ == context_ &&
            api_->read_field && api_->read_field(
                context_->plugin,
                handle_,
                field.RawHandle(),
                &value,
                sizeof(value)) == KEEL_RESULT_OK;
    }

    bool Same(const Entity& other) const noexcept
    {
        if (!LocallyValid() || !other.LocallyValid() || context_ != other.context_ ||
            api_ != other.api_ || !api_->equal)
        {
            return false;
        }
        KeelBool equal{};
        return api_->equal(context_->plugin, handle_, other.handle_, &equal) ==
                KEEL_RESULT_OK &&
            equal == KEEL_TRUE;
    }

private:
    friend class Service;

    bool LocallyValid() const noexcept
    {
        return handle_ && context_ &&
            context_->accepting_resources.load(std::memory_order_acquire) && api_;
    }

    void Adopt(
        std::shared_ptr<keels2::detail::ContextState> context,
        const KeelEntitiesApi* api,
        KeelEntityHandle handle,
        const KeelEntityInfo& info) noexcept
    {
        context_ = std::move(context);
        api_ = api;
        handle_ = handle;
        info_ = info;
    }

    void Clear() noexcept
    {
        context_.reset();
        api_ = nullptr;
        handle_ = 0;
        info_ = {};
    }

    void MoveFrom(Entity& other) noexcept
    {
        context_ = std::move(other.context_);
        api_ = std::exchange(other.api_, nullptr);
        handle_ = std::exchange(other.handle_, 0);
        info_ = std::exchange(other.info_, KeelEntityInfo{});
    }

    std::shared_ptr<keels2::detail::ContextState> context_;
    const KeelEntitiesApi* api_{};
    KeelEntityHandle handle_{};
    KeelEntityInfo info_{};
};

class Service final
{
public:
    KeelResult Connect(const Context& context) noexcept
    {
        api_ = nullptr;
        context_.reset();
        const void* raw{};
        const KeelResult result = context.QueryService(
            KEELS2_ENTITIES_SERVICE_NAME,
            KEELS2_ENTITIES_API_VERSION,
            &raw);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        const auto* api = static_cast<const KeelEntitiesApi*>(raw);
        if (!api || api->size != sizeof(KeelEntitiesApi) ||
            api->api_version != KEELS2_ENTITIES_API_VERSION || !api->find_by_index ||
            !api->find_by_source2_handle || !api->release || !api->describe ||
            !api->equal || !api->read_field)
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
            context_->accepting_resources.load(std::memory_order_acquire) && api_;
    }

    KeelResult Find(int index, Entity& output) const noexcept
    {
        static_cast<void>(output.Reset());
        if (index < 0)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        KeelEntityHandle handle{};
        const KeelResult result = api_->find_by_index(context_->plugin, index, &handle);
        return result == KEEL_RESULT_OK
            ? Adopt(handle, output)
            : result;
    }

    KeelResult FindSource2(uint32 source2_handle, Entity& output) const noexcept
    {
        static_cast<void>(output.Reset());
        if (source2_handle == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        KeelEntityHandle handle{};
        const KeelResult result = api_->find_by_source2_handle(
            context_->plugin,
            source2_handle,
            &handle);
        return result == KEEL_RESULT_OK
            ? Adopt(handle, output)
            : result;
    }

private:
    KeelResult Adopt(KeelEntityHandle handle, Entity& output) const noexcept
    {
        KeelEntityInfo info{};
        info.size = sizeof(info);
        const KeelResult result = api_->describe(context_->plugin, handle, &info);
        if (result != KEEL_RESULT_OK || info.size != sizeof(info) || info.index < 0 ||
            info.source2_handle == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE ||
            info.reserved != 0 || !info.epoch)
        {
            static_cast<void>(api_->release(context_->plugin, handle));
            return result == KEEL_RESULT_OK ? KEEL_RESULT_INCOMPATIBLE : result;
        }
        output.Adopt(context_, api_, handle, info);
        return KEEL_RESULT_OK;
    }

    std::shared_ptr<keels2::detail::ContextState> context_;
    const KeelEntitiesApi* api_{};
};

}

#endif
