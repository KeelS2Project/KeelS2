#ifndef KEELS2_KEELHOOK_HPP
#define KEELS2_KEELHOOK_HPP

#include <keels2/keelhook.h>
#include <keels2/plugin.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace keels2::kh
{

template <typename Type>
struct AggregateTraits;

template <typename Type>
concept DescribedAggregate =
    std::is_trivial_v<std::remove_cv_t<Type>> &&
    std::is_standard_layout_v<std::remove_cv_t<Type>> &&
    requires { AggregateTraits<std::remove_cv_t<Type>>::Fields(); };

template <typename Type, typename = void>
struct ValueType;

template <>
struct ValueType<void> : std::integral_constant<KeelHookValueType, KH_VALUE_VOID>
{
};

template <>
struct ValueType<bool> : std::integral_constant<KeelHookValueType, KH_VALUE_BOOL>
{
};

template <>
struct ValueType<std::int8_t> : std::integral_constant<KeelHookValueType, KH_VALUE_INT8>
{
};

template <>
struct ValueType<std::uint8_t> : std::integral_constant<KeelHookValueType, KH_VALUE_UINT8>
{
};

template <>
struct ValueType<std::int16_t> : std::integral_constant<KeelHookValueType, KH_VALUE_INT16>
{
};

template <>
struct ValueType<std::uint16_t> : std::integral_constant<KeelHookValueType, KH_VALUE_UINT16>
{
};

template <>
struct ValueType<std::int32_t> : std::integral_constant<KeelHookValueType, KH_VALUE_INT32>
{
};

template <>
struct ValueType<std::uint32_t> : std::integral_constant<KeelHookValueType, KH_VALUE_UINT32>
{
};

template <>
struct ValueType<std::int64_t> : std::integral_constant<KeelHookValueType, KH_VALUE_INT64>
{
};

template <>
struct ValueType<std::uint64_t> : std::integral_constant<KeelHookValueType, KH_VALUE_UINT64>
{
};

template <typename Type>
struct ValueType<Type*> : std::integral_constant<KeelHookValueType, KH_VALUE_POINTER>
{
};

template <>
struct ValueType<float> : std::integral_constant<KeelHookValueType, KH_VALUE_FLOAT32>
{
};

template <>
struct ValueType<double> : std::integral_constant<KeelHookValueType, KH_VALUE_FLOAT64>
{
};

template <typename Type>
struct ValueType<Type, std::enable_if_t<DescribedAggregate<Type>>>
    : std::integral_constant<KeelHookValueType, KH_VALUE_AGGREGATE>
{
};

template <typename Type>
inline constexpr KeelHookValueType ValueTypeV = ValueType<std::remove_cv_t<Type>>::value;

template <typename Type>
struct AggregateMetadata;

template <typename Type>
consteval const KeelHookAggregate* AggregateDescriptor()
{
    using Plain = std::remove_cv_t<Type>;
    if constexpr (DescribedAggregate<Plain>)
    {
        return &AggregateMetadata<Plain>::value;
    }
    return nullptr;
}

struct AggregateFieldDefinition
{
    KeelHookAggregateField descriptor{};
    std::size_t element_size{};
};

template <std::size_t Count>
struct AggregateFieldList
{
    std::array<KeelHookAggregateField, Count> descriptors{};
    std::array<std::size_t, Count> element_sizes{};

    constexpr std::size_t size() const noexcept
    {
        return Count;
    }

    constexpr const KeelHookAggregateField& operator[](std::size_t index) const noexcept
    {
        return descriptors[index];
    }

    constexpr const KeelHookAggregateField* data() const noexcept
    {
        return descriptors.data();
    }
};

template <typename Type>
consteval AggregateFieldDefinition Field(std::uint32_t offset, std::uint32_t array_length = 1)
{
    using Plain = std::remove_cv_t<Type>;
    static_assert(!std::is_void_v<Plain>);
    static_assert(!std::is_reference_v<Plain>);
    static_assert(std::is_pointer_v<Plain> || std::is_arithmetic_v<Plain> || DescribedAggregate<Plain>);
    return {
        {
            sizeof(KeelHookAggregateField),
            ValueTypeV<Plain>,
            offset,
            array_length,
            AggregateDescriptor<Plain>()
        },
        sizeof(Plain)
    };
}

template <typename... Types>
consteval auto Fields(Types... fields)
{
    static_assert((std::is_same_v<Types, AggregateFieldDefinition> && ...));
    return AggregateFieldList<sizeof...(Types)>{
        std::array<KeelHookAggregateField, sizeof...(Types)>{fields.descriptor...},
        std::array<std::size_t, sizeof...(Types)>{fields.element_size...}
    };
}

template <typename Type>
struct AggregateMetadata
{
    using Plain = std::remove_cv_t<Type>;
    static_assert(DescribedAggregate<Plain>);
    static_assert(sizeof(void*) == 8);
    static_assert(sizeof(Plain) > 0 && sizeof(Plain) <= KEELHOOK_MAX_AGGREGATE_SIZE);
    static_assert(alignof(Plain) <= KEELHOOK_MAX_AGGREGATE_ALIGNMENT);
    static inline constexpr auto fields = AggregateTraits<Plain>::Fields();
    static_assert(fields.size() > 0 && fields.size() <= KEELHOOK_MAX_AGGREGATE_FIELDS);

    static consteval std::size_t ScalarFieldSize(KeelHookValueType type)
    {
        switch (type)
        {
            case KH_VALUE_BOOL:
            case KH_VALUE_INT8:
            case KH_VALUE_UINT8: return 1;
            case KH_VALUE_INT16:
            case KH_VALUE_UINT16: return 2;
            case KH_VALUE_INT32:
            case KH_VALUE_UINT32:
            case KH_VALUE_FLOAT32: return 4;
            case KH_VALUE_INT64:
            case KH_VALUE_UINT64:
            case KH_VALUE_POINTER:
            case KH_VALUE_FLOAT64: return 8;
            default: return 0;
        }
    }

    static consteval bool ValidFields()
    {
        std::uint32_t previous{};
        for (std::size_t index{}; index < fields.size(); ++index)
        {
            const auto& field = fields[index];
            const bool aggregate = field.type == KH_VALUE_AGGREGATE;
            const std::size_t field_size = fields.element_sizes[index];
            const std::size_t scalar_size = ScalarFieldSize(field.type);
            if (field.size != sizeof(KeelHookAggregateField) || field.array_length == 0 ||
                field.offset >= sizeof(Plain) || (index != 0 && field.offset < previous) ||
                field_size == 0 || field.array_length > (sizeof(Plain) - field.offset) / field_size ||
                (!aggregate && (scalar_size != field_size || field.aggregate != nullptr)) ||
                (aggregate && scalar_size != 0))
            {
                return false;
            }
            previous = field.offset;
        }
        return true;
    }

    static_assert(ValidFields());
    static inline constexpr KeelHookAggregate value{
        sizeof(KeelHookAggregate),
        sizeof(Plain),
        static_cast<std::uint32_t>(fields.size()),
        0,
        fields.data()
    };
};

template <typename Signature>
struct Prototype;

template <typename Return, typename... Arguments>
struct Prototype<Return(Arguments...)>
{
    static_assert(sizeof...(Arguments) <= KEELHOOK_MAX_ARGUMENTS);
    static inline constexpr std::array<KeelHookValueType, sizeof...(Arguments)> arguments{
        ValueTypeV<Arguments>...
    };
    static inline constexpr std::array<const KeelHookAggregate*, sizeof...(Arguments)> aggregates{
        AggregateDescriptor<Arguments>()...
    };
    static inline constexpr KeelHookPrototype value{
        sizeof(KeelHookPrototype),
        KH_CALL_NATIVE,
        ValueTypeV<Return>,
        static_cast<std::uint32_t>(sizeof...(Arguments)),
        arguments.data(),
        AggregateDescriptor<Return>(),
        aggregates.data(),
        static_cast<std::uint32_t>(sizeof...(Arguments)),
        0
    };
};

template <typename Signature>
struct MethodPrototype;

template <typename Return, typename... Arguments>
struct MethodPrototype<Return(Arguments...)>
{
    static_assert(sizeof...(Arguments) + 1 <= KEELHOOK_MAX_ARGUMENTS);
    static inline constexpr std::array<KeelHookValueType, sizeof...(Arguments) + 1> arguments{
        KH_VALUE_POINTER,
        ValueTypeV<Arguments>...
    };
    static inline constexpr std::array<const KeelHookAggregate*, sizeof...(Arguments) + 1> aggregates{
        nullptr,
        AggregateDescriptor<Arguments>()...
    };
    static inline constexpr KeelHookPrototype value{
        sizeof(KeelHookPrototype),
        KH_CALL_NATIVE,
        ValueTypeV<Return>,
        static_cast<std::uint32_t>(sizeof...(Arguments) + 1),
        arguments.data(),
        AggregateDescriptor<Return>(),
        aggregates.data(),
        static_cast<std::uint32_t>(sizeof...(Arguments) + 1),
        0
    };
};

template <typename Type>
bool ValidValue(const KeelHookValue& value) noexcept
{
    using Plain = std::remove_cv_t<Type>;
    if (value.type != ValueTypeV<Plain> || value.reserved != 0)
    {
        return false;
    }
    if constexpr (DescribedAggregate<Plain>)
    {
        return value.scalar.aggregate.data &&
            value.scalar.aggregate.size == sizeof(Plain) &&
            value.scalar.aggregate.reserved == 0;
    }
    else
    {
        return true;
    }
}

template <typename Type>
Type Read(const KeelHookValue& value)
{
    using Plain = std::remove_cv_t<Type>;
    if (!ValidValue<Plain>(value))
    {
        return Plain{};
    }
    if constexpr (DescribedAggregate<Plain>)
    {
        Plain result{};
        std::memcpy(&result, value.scalar.aggregate.data, sizeof(result));
        return result;
    }
    else if constexpr (std::is_same_v<Plain, bool>)
    {
        return value.scalar.boolean == KEEL_TRUE;
    }
    else if constexpr (std::is_same_v<Plain, std::int8_t>)
    {
        return value.scalar.int8;
    }
    else if constexpr (std::is_same_v<Plain, std::uint8_t>)
    {
        return value.scalar.uint8;
    }
    else if constexpr (std::is_same_v<Plain, std::int16_t>)
    {
        return value.scalar.int16;
    }
    else if constexpr (std::is_same_v<Plain, std::uint16_t>)
    {
        return value.scalar.uint16;
    }
    else if constexpr (std::is_same_v<Plain, std::int32_t>)
    {
        return value.scalar.int32;
    }
    else if constexpr (std::is_same_v<Plain, std::uint32_t>)
    {
        return value.scalar.uint32;
    }
    else if constexpr (std::is_same_v<Plain, std::int64_t>)
    {
        return value.scalar.int64;
    }
    else if constexpr (std::is_same_v<Plain, std::uint64_t>)
    {
        return value.scalar.uint64;
    }
    else if constexpr (std::is_pointer_v<Plain>)
    {
        if constexpr (std::is_function_v<std::remove_pointer_t<Plain>>)
        {
            static_assert(sizeof(Plain) == sizeof(value.scalar.pointer));
            Plain result{};
            std::memcpy(&result, &value.scalar.pointer, sizeof(result));
            return result;
        }
        else
        {
            return static_cast<Plain>(value.scalar.pointer);
        }
    }
    else if constexpr (std::is_same_v<Plain, float>)
    {
        return value.scalar.float32;
    }
    else
    {
        static_assert(std::is_same_v<Plain, double>);
        return value.scalar.float64;
    }
}

template <typename Type>
bool Write(KeelHookValue& value, Type input)
{
    using Plain = std::remove_cv_t<Type>;
    if (!ValidValue<Plain>(value))
    {
        return false;
    }
    if constexpr (DescribedAggregate<Plain>)
    {
        std::memcpy(value.scalar.aggregate.data, &input, sizeof(input));
    }
    else if constexpr (std::is_same_v<Plain, bool>)
    {
        value.scalar.boolean = input ? KEEL_TRUE : KEEL_FALSE;
    }
    else if constexpr (std::is_same_v<Plain, std::int8_t>)
    {
        value.scalar.int8 = input;
    }
    else if constexpr (std::is_same_v<Plain, std::uint8_t>)
    {
        value.scalar.uint8 = input;
    }
    else if constexpr (std::is_same_v<Plain, std::int16_t>)
    {
        value.scalar.int16 = input;
    }
    else if constexpr (std::is_same_v<Plain, std::uint16_t>)
    {
        value.scalar.uint16 = input;
    }
    else if constexpr (std::is_same_v<Plain, std::int32_t>)
    {
        value.scalar.int32 = input;
    }
    else if constexpr (std::is_same_v<Plain, std::uint32_t>)
    {
        value.scalar.uint32 = input;
    }
    else if constexpr (std::is_same_v<Plain, std::int64_t>)
    {
        value.scalar.int64 = input;
    }
    else if constexpr (std::is_same_v<Plain, std::uint64_t>)
    {
        value.scalar.uint64 = input;
    }
    else if constexpr (std::is_pointer_v<Plain>)
    {
        if constexpr (std::is_function_v<std::remove_pointer_t<Plain>>)
        {
            static_assert(sizeof(Plain) == sizeof(value.scalar.pointer));
            std::memcpy(&value.scalar.pointer, &input, sizeof(input));
        }
        else
        {
            value.scalar.pointer = const_cast<void*>(static_cast<const void*>(input));
        }
    }
    else if constexpr (std::is_same_v<Plain, float>)
    {
        value.scalar.float32 = input;
    }
    else
    {
        static_assert(std::is_same_v<Plain, double>);
        value.scalar.float64 = input;
    }
    value.reserved = 0;
    return true;
}

enum class Action : std::uint32_t
{
    Continue = KH_ACTION_CONTINUE,
    Override = KH_ACTION_OVERRIDE,
    Supersede = KH_ACTION_SUPERSEDE
};

class Frame final
{
public:
    explicit Frame(KeelHookFrame* frame) noexcept
        : frame_(frame)
    {
    }

    explicit operator bool() const noexcept
    {
        return frame_ && frame_->size == sizeof(KeelHookFrame) &&
            frame_->argument_count <= KEELHOOK_MAX_ARGUMENTS &&
            (frame_->argument_count == 0 || frame_->arguments);
    }

    KeelHookPhase Phase() const noexcept
    {
        return *this ? frame_->phase : 0;
    }

    KeelHookTargetHandle TargetHandle() const noexcept
    {
        return *this ? frame_->target : 0;
    }

    std::size_t ArgumentCount() const noexcept
    {
        return *this ? frame_->argument_count : 0;
    }

    bool OriginalCalled() const noexcept
    {
        return *this && (frame_->flags & KH_FRAME_ORIGINAL_CALLED) != 0;
    }

    template <typename Type>
    std::optional<std::remove_cv_t<Type>> Argument(std::size_t index) const
    {
        using Plain = std::remove_cv_t<Type>;
        if (!*this || index >= frame_->argument_count ||
            !ValidValue<Plain>(frame_->arguments[index]))
        {
            return std::nullopt;
        }
        return Read<Plain>(frame_->arguments[index]);
    }

    template <typename Type>
    bool SetArgument(std::size_t index, Type value) noexcept
    {
        return *this && index < frame_->argument_count &&
            Write(frame_->arguments[index], value);
    }

    template <typename Type>
    std::optional<std::remove_cv_t<Type>> Result() const
    {
        using Plain = std::remove_cv_t<Type>;
        static_assert(!std::is_void_v<Plain>);
        if (!*this || !ValidValue<Plain>(frame_->result))
        {
            return std::nullopt;
        }
        return Read<Plain>(frame_->result);
    }

    template <typename Type>
    bool SetResult(Type value) noexcept
    {
        return *this && Write(frame_->result, value);
    }

    KeelHookFrame* Raw() const noexcept
    {
        return frame_;
    }

private:
    KeelHookFrame* frame_{};
};

class TargetSpec final
{
public:
    static TargetSpec Address(void* address, std::int64_t offset = 0) noexcept
    {
        TargetSpec result;
        result.value_.source = KH_TARGET_ADDRESS;
        result.value_.mechanism = KH_MECHANISM_DETOUR;
        result.value_.address = address;
        result.value_.offset = offset;
        return result;
    }

    static TargetSpec Symbol(
        const char* module,
        const char* symbol,
        std::int64_t offset = 0) noexcept
    {
        TargetSpec result;
        result.value_.source = KH_TARGET_SYMBOL;
        result.value_.mechanism = KH_MECHANISM_DETOUR;
        result.value_.module = module;
        result.value_.symbol = symbol;
        result.value_.offset = offset;
        return result;
    }

    static TargetSpec Pattern(
        const char* module,
        const char* pattern,
        const char* profile,
        std::uint32_t occurrence = 0,
        std::int64_t offset = 0) noexcept
    {
        TargetSpec result;
        result.value_.source = KH_TARGET_PATTERN;
        result.value_.mechanism = KH_MECHANISM_DETOUR;
        result.value_.module = module;
        result.value_.pattern = pattern;
        result.value_.profile = profile;
        result.value_.offset = offset;
        result.value_.occurrence = occurrence;
        return result;
    }

    const KeelHookTargetSpec& Raw() const noexcept
    {
        return value_;
    }

    TargetSpec& AsMethod() & noexcept
    {
        value_.flags |= KH_TARGET_METHOD;
        return *this;
    }

    TargetSpec&& AsMethod() && noexcept
    {
        value_.flags |= KH_TARGET_METHOD;
        return std::move(*this);
    }

private:
    TargetSpec() noexcept
    {
        value_.size = sizeof(KeelHookTargetSpec);
    }

    KeelHookTargetSpec value_{};
};

class VirtualTargetSpec final
{
public:
    static VirtualTargetSpec Shared(
        void* instance,
        std::uint32_t index,
        const char* profile = nullptr) noexcept
    {
        VirtualTargetSpec result;
        result.value_.mechanism = KH_MECHANISM_VIRTUAL;
        result.value_.instance = instance;
        result.value_.index = index;
        result.value_.profile = profile;
        return result;
    }

    static VirtualTargetSpec Instance(
        void* instance,
        std::uint32_t index,
        std::uint32_t table_size,
        const char* profile = nullptr) noexcept
    {
        VirtualTargetSpec result;
        result.value_.mechanism = KH_MECHANISM_VIRTUAL_INSTANCE;
        result.value_.instance = instance;
        result.value_.index = index;
        result.value_.table_size = table_size;
        result.value_.profile = profile;
        return result;
    }

    const KeelHookVirtualTargetSpec& Raw() const noexcept
    {
        return value_;
    }

private:
    VirtualTargetSpec() noexcept
    {
        value_.size = sizeof(KeelHookVirtualTargetSpec);
    }

    KeelHookVirtualTargetSpec value_{};
};

namespace detail
{

struct TargetState final
{
    ~TargetState()
    {
        static_cast<void>(Reset());
    }

    KeelResult Reset() noexcept
    {
        if (!handle)
        {
            return KEEL_RESULT_OK;
        }
        if (!context ||
            !context->accepting_resources.load(std::memory_order_acquire) ||
            !context->api || !api ||
            !api->release_target)
        {
            Clear();
            return KEEL_RESULT_NOT_READY;
        }
        const KeelResult result = api->release_target(context->plugin, handle);
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            Clear();
        }
        return result;
    }

    void Clear() noexcept
    {
        api = nullptr;
        context.reset();
        handle = 0;
    }

    std::shared_ptr<keels2::detail::ContextState> context;
    const KeelHookApi* api{};
    KeelHookTargetHandle handle{};
};

}

class Service;
class Callback;

class Target final
{
public:
    Target() = default;
    ~Target() = default;
    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;
    Target(Target&&) noexcept = default;
    Target& operator=(Target&&) noexcept = default;

    explicit operator bool() const noexcept
    {
        return state_ && state_->handle && state_->context &&
            state_->context->accepting_resources.load(std::memory_order_acquire);
    }

    KeelHookTargetHandle Handle() const noexcept
    {
        return *this ? state_->handle : 0;
    }

    KeelResult Reset() noexcept
    {
        if (!state_)
        {
            return KEEL_RESULT_OK;
        }
        const KeelResult result = state_->Reset();
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            state_.reset();
        }
        return result;
    }

private:
    friend class Service;
    friend class Callback;
    std::shared_ptr<detail::TargetState> state_;
};

class Callback final
{
public:
    Callback() = default;
    ~Callback()
    {
        static_cast<void>(Reset());
    }

    Callback(const Callback&) = delete;
    Callback& operator=(const Callback&) = delete;

    Callback(Callback&& other) noexcept
    {
        MoveFrom(other);
    }

    Callback& operator=(Callback&& other) noexcept
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
        return handle_ && target_ && target_->context &&
            target_->context->accepting_resources.load(std::memory_order_acquire);
    }

    KeelHookCallbackHandle Handle() const noexcept
    {
        return *this ? handle_ : 0;
    }

    KeelResult Reset() noexcept
    {
        if (!handle_)
        {
            return KEEL_RESULT_OK;
        }
        if (!target_ || !target_->context ||
            !target_->context->accepting_resources.load(std::memory_order_acquire) ||
            !target_->context->api ||
            !target_->api || !target_->api->remove_callback)
        {
            Clear();
            return KEEL_RESULT_NOT_READY;
        }
        const KeelResult result = target_->api->remove_callback(
            target_->context->plugin,
            handle_);
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            Clear();
        }
        return result;
    }

private:
    friend class Service;

    void Adopt(std::shared_ptr<detail::TargetState> target, KeelHookCallbackHandle handle) noexcept
    {
        target_ = std::move(target);
        handle_ = handle;
    }

    void Clear() noexcept
    {
        handle_ = 0;
        target_.reset();
    }

    void MoveFrom(Callback& other) noexcept
    {
        target_ = std::move(other.target_);
        handle_ = std::exchange(other.handle_, 0);
    }

    std::shared_ptr<detail::TargetState> target_;
    KeelHookCallbackHandle handle_{};
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
            KEELHOOK_SERVICE_NAME,
            KEELHOOK_API_VERSION,
            &service);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        const auto* api = static_cast<const KeelHookApi*>(service);
        if (!api || api->size != sizeof(KeelHookApi) ||
            api->api_version != KEELHOOK_API_VERSION || !api->resolve_target ||
            !api->release_target || !api->add_callback || !api->remove_callback ||
            !api->resolve_virtual_target)
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

    template <typename Signature>
    KeelResult Resolve(const TargetSpec& spec, Target& output)
    {
        return ResolveTarget(spec, Prototype<Signature>::value, output);
    }

    template <typename Signature>
    KeelResult ResolveMethod(const TargetSpec& spec, Target& output)
    {
        TargetSpec method = spec;
        method.AsMethod();
        return ResolveTarget(method, MethodPrototype<Signature>::value, output);
    }

    template <typename Signature>
    KeelResult Resolve(const VirtualTargetSpec& spec, Target& output)
    {
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        if (output)
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        if (!api_->resolve_virtual_target)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        KeelHookTargetHandle handle{};
        const KeelResult result = api_->resolve_virtual_target(
            context_->plugin,
            &spec.Raw(),
            &MethodPrototype<Signature>::value,
            &handle);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        try
        {
            auto state = std::make_shared<detail::TargetState>();
            state->context = context_;
            state->api = api_;
            state->handle = handle;
            output.state_ = std::move(state);
        }
        catch (...)
        {
            static_cast<void>(api_->release_target(context_->plugin, handle));
            throw;
        }
        return KEEL_RESULT_OK;
    }

    KeelResult AddCallback(
        const Target& target,
        const KeelHookCallbackSpec& spec,
        Callback& output) const noexcept
    {
        if (!*this || !target || target.state_->api != api_ ||
            target.state_->context != context_)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (output)
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        KeelHookCallbackHandle handle{};
        const KeelResult result = api_->add_callback(
            context_->plugin,
            target.state_->handle,
            &spec,
            &handle);
        if (result == KEEL_RESULT_OK)
        {
            output.Adopt(target.state_, handle);
        }
        return result;
    }

    template <auto Method, typename Owner>
    KeelResult AddCallback(
        const Target& target,
        Callback& output,
        std::uint32_t phases,
        std::int32_t priority,
        Owner& owner) const noexcept
    {
        static_assert(std::is_invocable_v<decltype(Method), Owner&, Frame&>);
        using Result = std::invoke_result_t<decltype(Method), Owner&, Frame&>;
        static_assert(std::is_same_v<Result, Action> || std::is_same_v<Result, KeelHookAction>);
        const KeelHookCallbackSpec spec{
            sizeof(KeelHookCallbackSpec),
            phases,
            priority,
            0,
            [](KeelHookFrame* frame, void* user_data) -> KeelHookAction {
                Frame view(frame);
                if constexpr (std::is_same_v<Result, Action>)
                {
                    return static_cast<KeelHookAction>(
                        std::invoke(Method, *static_cast<Owner*>(user_data), view));
                }
                else
                {
                    return std::invoke(Method, *static_cast<Owner*>(user_data), view);
                }
            },
            &owner
        };
        return AddCallback(target, spec, output);
    }

private:
    KeelResult ResolveTarget(
        const TargetSpec& spec,
        const KeelHookPrototype& prototype,
        Target& output)
    {
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        if (output)
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        KeelHookTargetHandle handle{};
        const KeelResult result = api_->resolve_target(
            context_->plugin,
            &spec.Raw(),
            &prototype,
            &handle);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        try
        {
            auto state = std::make_shared<detail::TargetState>();
            state->context = context_;
            state->api = api_;
            state->handle = handle;
            output.state_ = std::move(state);
        }
        catch (...)
        {
            static_cast<void>(api_->release_target(context_->plugin, handle));
            throw;
        }
        return KEEL_RESULT_OK;
    }

    std::shared_ptr<keels2::detail::ContextState> context_;
    const KeelHookApi* api_{};
};

}

#endif
