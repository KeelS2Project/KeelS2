#ifndef KEELS2_SCHEMA_HPP
#define KEELS2_SCHEMA_HPP

#include <keels2/plugin.hpp>
#include <keels2/schema.h>

#include <tier0/platform.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

namespace keels2::entities
{

class Entity;

}

namespace keels2::schema
{

namespace detail
{

template <typename Value>
inline constexpr bool kSupportedValue =
    std::is_same_v<std::remove_cv_t<Value>, char> ||
    std::is_same_v<std::remove_cv_t<Value>, int8> ||
    std::is_same_v<std::remove_cv_t<Value>, uint8> ||
    std::is_same_v<std::remove_cv_t<Value>, int16> ||
    std::is_same_v<std::remove_cv_t<Value>, uint16> ||
    std::is_same_v<std::remove_cv_t<Value>, int32> ||
    std::is_same_v<std::remove_cv_t<Value>, uint32> ||
    std::is_same_v<std::remove_cv_t<Value>, int64> ||
    std::is_same_v<std::remove_cv_t<Value>, uint64> ||
    std::is_same_v<std::remove_cv_t<Value>, float32> ||
    std::is_same_v<std::remove_cv_t<Value>, float64> ||
    std::is_same_v<std::remove_cv_t<Value>, bool>;

template <typename Value>
constexpr KeelSchemaValueType ValueType() noexcept
{
    using Type = std::remove_cv_t<Value>;
    static_assert(kSupportedValue<Type>);
    if constexpr (std::is_same_v<Type, char>)
    {
        return KEELS2_SCHEMA_CHAR;
    }
    else if constexpr (std::is_same_v<Type, int8>)
    {
        return KEELS2_SCHEMA_INT8;
    }
    else if constexpr (std::is_same_v<Type, uint8>)
    {
        return KEELS2_SCHEMA_UINT8;
    }
    else if constexpr (std::is_same_v<Type, int16>)
    {
        return KEELS2_SCHEMA_INT16;
    }
    else if constexpr (std::is_same_v<Type, uint16>)
    {
        return KEELS2_SCHEMA_UINT16;
    }
    else if constexpr (std::is_same_v<Type, int32>)
    {
        return KEELS2_SCHEMA_INT32;
    }
    else if constexpr (std::is_same_v<Type, uint32>)
    {
        return KEELS2_SCHEMA_UINT32;
    }
    else if constexpr (std::is_same_v<Type, int64>)
    {
        return KEELS2_SCHEMA_INT64;
    }
    else if constexpr (std::is_same_v<Type, uint64>)
    {
        return KEELS2_SCHEMA_UINT64;
    }
    else if constexpr (std::is_same_v<Type, float32>)
    {
        return KEELS2_SCHEMA_FLOAT32;
    }
    else if constexpr (std::is_same_v<Type, float64>)
    {
        return KEELS2_SCHEMA_FLOAT64;
    }
    else
    {
        return KEELS2_SCHEMA_BOOL;
    }
}

}

template <typename Value>
class Field final
{
public:
    static_assert(detail::kSupportedValue<Value>);

    Field() = default;

    ~Field()
    {
        static_cast<void>(Reset());
    }

    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;

    Field(Field&& other) noexcept
    {
        MoveFrom(other);
    }

    Field& operator=(Field&& other) noexcept
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
        return handle_ && context_ &&
            context_->accepting_resources.load(std::memory_order_acquire) && api_;
    }

    KeelResult Reset() noexcept
    {
        if (!handle_)
        {
            return KEEL_RESULT_OK;
        }
        if (!context_ ||
            !context_->accepting_resources.load(std::memory_order_acquire) ||
            !api_ || !api_->release_field)
        {
            Clear();
            return KEEL_RESULT_NOT_READY;
        }
        const KeelResult result = api_->release_field(context_->plugin, handle_);
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            Clear();
        }
        return result;
    }

    int Offset() const noexcept
    {
        return *this ? info_.offset : -1;
    }

    const char* ClassName() const noexcept
    {
        return *this ? info_.class_name : nullptr;
    }

    const char* FieldName() const noexcept
    {
        return *this ? info_.field_name : nullptr;
    }

    const char* ModuleName() const noexcept
    {
        return *this ? info_.module_name : nullptr;
    }

    const char* CompatibilityProfile() const noexcept
    {
        return *this ? info_.compatibility_profile : nullptr;
    }

private:
    friend class Service;
    friend class keels2::entities::Entity;

    void Adopt(
        std::shared_ptr<keels2::detail::ContextState> context,
        const KeelSchemaApi* api,
        KeelSchemaFieldHandle handle,
        const KeelSchemaFieldInfo& info) noexcept
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

    void MoveFrom(Field& other) noexcept
    {
        context_ = std::move(other.context_);
        api_ = std::exchange(other.api_, nullptr);
        handle_ = std::exchange(other.handle_, 0);
        info_ = std::exchange(other.info_, KeelSchemaFieldInfo{});
    }

    KeelSchemaFieldHandle RawHandle() const noexcept
    {
        return *this ? handle_ : 0;
    }

    std::shared_ptr<keels2::detail::ContextState> context_;
    const KeelSchemaApi* api_{};
    KeelSchemaFieldHandle handle_{};
    KeelSchemaFieldInfo info_{};
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
            KEELS2_SCHEMA_SERVICE_NAME,
            KEELS2_SCHEMA_API_VERSION,
            &raw);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        const auto* api = static_cast<const KeelSchemaApi*>(raw);
        if (!api || api->size != sizeof(KeelSchemaApi) ||
            api->api_version != KEELS2_SCHEMA_API_VERSION || !api->resolve_field ||
            !api->release_field || !api->describe_field)
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

    template <typename Value>
    KeelResult Resolve(
        const char* class_name,
        const char* field_name,
        Field<Value>& output) const noexcept
    {
        static_assert(detail::kSupportedValue<Value>);
        static_cast<void>(output.Reset());
        if (!class_name || !class_name[0] || !field_name || !field_name[0])
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        const KeelSchemaFieldSpec spec{
            sizeof(KeelSchemaFieldSpec),
            KEELS2_SCHEMA_MODULE_SERVER,
            detail::ValueType<Value>(),
            0,
            class_name,
            field_name
        };
        KeelSchemaFieldHandle handle{};
        KeelResult result = api_->resolve_field(context_->plugin, &spec, &handle);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        KeelSchemaFieldInfo info{};
        info.size = sizeof(info);
        result = api_->describe_field(context_->plugin, handle, &info);
        if (result != KEEL_RESULT_OK || info.size != sizeof(info) ||
            info.module != KEELS2_SCHEMA_MODULE_SERVER ||
            info.value_type != detail::ValueType<Value>() ||
            info.value_size != sizeof(Value) || info.value_alignment != alignof(Value) ||
            info.offset < 0 || info.reserved != 0 || !info.class_name ||
            std::strcmp(info.class_name, class_name) != 0 || !info.field_name ||
            std::strcmp(info.field_name, field_name) != 0 || !info.module_name ||
            !info.module_name[0] || !info.compatibility_profile ||
            !info.compatibility_profile[0])
        {
            static_cast<void>(api_->release_field(context_->plugin, handle));
            return result == KEEL_RESULT_OK ? KEEL_RESULT_INCOMPATIBLE : result;
        }
        output.Adopt(context_, api_, handle, info);
        return KEEL_RESULT_OK;
    }

private:
    std::shared_ptr<keels2::detail::ContextState> context_;
    const KeelSchemaApi* api_{};
};

}

#endif
