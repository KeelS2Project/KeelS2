#ifndef KEELS2_KEELHOOK_HPP
#define KEELS2_KEELHOOK_HPP

#include <keels2/keelhook.h>
#include <keels2/plugin.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

enum PluginResult : std::uint32_t
{
    plugin_continue = KH_ACTION_CONTINUE,
    plugin_override = KH_ACTION_OVERRIDE,
    plugin_supersede = KH_ACTION_SUPERSEDE
};

namespace keels2::kh
{

template <typename Type>
struct AggregateTraits;

template <typename Type>
struct ValueAdapter;

template <typename Type>
concept AdaptedValue = requires(
    const KeelHookValue& source,
    KeelHookValue& destination,
    const std::remove_cv_t<Type>& input)
{
    { ValueAdapter<std::remove_cv_t<Type>>::type } -> std::convertible_to<KeelHookValueType>;
    { ValueAdapter<std::remove_cv_t<Type>>::Read(source) } noexcept ->
        std::same_as<std::remove_cv_t<Type>>;
    { ValueAdapter<std::remove_cv_t<Type>>::Fallback() } noexcept ->
        std::same_as<std::remove_cv_t<Type>>;
    { ValueAdapter<std::remove_cv_t<Type>>::Write(destination, input) } noexcept ->
        std::same_as<bool>;
} && std::is_nothrow_copy_constructible_v<std::remove_cv_t<Type>>;

template <typename Type>
concept DescribedAggregate =
    std::is_trivial_v<std::remove_cv_t<Type>> &&
    std::is_standard_layout_v<std::remove_cv_t<Type>> &&
    requires { AggregateTraits<std::remove_cv_t<Type>>::Fields(); };

template <typename Type>
concept ManagedObject =
    (std::is_class_v<std::remove_cv_t<Type>> ||
        std::is_union_v<std::remove_cv_t<Type>>) &&
    !std::is_trivial_v<std::remove_cv_t<Type>> &&
    std::is_default_constructible_v<std::remove_cv_t<Type>> &&
    std::is_copy_constructible_v<std::remove_cv_t<Type>> &&
    std::is_copy_assignable_v<std::remove_cv_t<Type>> &&
    std::is_nothrow_destructible_v<std::remove_cv_t<Type>>;

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

template <typename Type>
consteval KeelHookValueType IntegralValueType()
{
    static_assert(std::is_integral_v<Type>);
    static_assert(!std::is_same_v<Type, bool>);
    if constexpr (sizeof(Type) == 1)
    {
        return std::is_signed_v<Type> ? KH_VALUE_INT8 : KH_VALUE_UINT8;
    }
    else if constexpr (sizeof(Type) == 2)
    {
        return std::is_signed_v<Type> ? KH_VALUE_INT16 : KH_VALUE_UINT16;
    }
    else if constexpr (sizeof(Type) == 4)
    {
        return std::is_signed_v<Type> ? KH_VALUE_INT32 : KH_VALUE_UINT32;
    }
    else
    {
        static_assert(sizeof(Type) == 8);
        return std::is_signed_v<Type> ? KH_VALUE_INT64 : KH_VALUE_UINT64;
    }
}

template <typename Type>
struct ValueType<Type, std::enable_if_t<
    std::is_integral_v<Type> && !std::is_same_v<Type, bool> && !AdaptedValue<Type>>>
    : std::integral_constant<KeelHookValueType, IntegralValueType<Type>()>
{
};

template <typename Type>
struct ValueType<Type*> : std::integral_constant<KeelHookValueType, KH_VALUE_POINTER>
{
};

template <typename Type>
struct ValueType<Type&> : std::integral_constant<KeelHookValueType, KH_VALUE_POINTER>
{
};

template <typename Type>
struct ValueType<Type, std::enable_if_t<std::is_enum_v<Type> && !AdaptedValue<Type>>>
    : ValueType<std::underlying_type_t<Type>>
{
};

template <typename Type>
struct ValueType<Type, std::enable_if_t<AdaptedValue<Type>>>
    : std::integral_constant<KeelHookValueType, ValueAdapter<Type>::type>
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
struct ValueType<Type, std::enable_if_t<
    DescribedAggregate<Type> && !AdaptedValue<Type>>>
    : std::integral_constant<KeelHookValueType, KH_VALUE_AGGREGATE>
{
};

template <typename Type>
struct ValueType<Type, std::enable_if_t<
    ManagedObject<Type> && !AdaptedValue<Type>>>
    : std::integral_constant<KeelHookValueType, KH_VALUE_AGGREGATE>
{
};

template <typename Type>
inline constexpr KeelHookValueType ValueTypeV = ValueType<std::remove_cv_t<Type>>::value;

template <typename Type>
struct AggregateMetadata;

template <typename Type>
struct ObjectMetadata;

template <typename Type>
consteval const KeelHookAggregate* AggregateDescriptor()
{
    if constexpr (std::is_reference_v<Type>)
    {
        return nullptr;
    }
    else
    {
        using Plain = std::remove_cv_t<Type>;
        if constexpr (DescribedAggregate<Plain> && !AdaptedValue<Plain>)
        {
            return &AggregateMetadata<Plain>::value;
        }
        return nullptr;
    }
}

template <typename Type>
consteval const KeelHookObject* ObjectDescriptor()
{
    if constexpr (std::is_reference_v<Type>)
    {
        return nullptr;
    }
    else
    {
        using Plain = std::remove_cv_t<Type>;
        if constexpr (ManagedObject<Plain> && !AdaptedValue<Plain>)
        {
            return &ObjectMetadata<Plain>::value;
        }
        return nullptr;
    }
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

template <typename Type>
consteval const char* ObjectIdentity() noexcept
{
#if defined(_MSC_VER)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

template <typename Type>
struct ObjectMetadata
{
    using Plain = std::remove_cv_t<Type>;
    static_assert(ManagedObject<Plain>);
    static_assert(sizeof(Plain) > 0 && sizeof(Plain) <= KEELHOOK_MAX_AGGREGATE_SIZE);
    static_assert(alignof(Plain) <= KEELHOOK_MAX_AGGREGATE_ALIGNMENT);

    static KeelBool DefaultConstruct(void* destination) noexcept
    {
        if (!destination)
        {
            return KEEL_FALSE;
        }
        try
        {
            std::construct_at(static_cast<Plain*>(destination));
            return KEEL_TRUE;
        }
        catch (...)
        {
            return KEEL_FALSE;
        }
    }

    static KeelBool CopyConstruct(void* destination, const void* source) noexcept
    {
        if (!destination || !source)
        {
            return KEEL_FALSE;
        }
        try
        {
            std::construct_at(
                static_cast<Plain*>(destination),
                *static_cast<const Plain*>(source));
            return KEEL_TRUE;
        }
        catch (...)
        {
            return KEEL_FALSE;
        }
    }

    static KeelBool CopyAssign(void* destination, const void* source) noexcept
    {
        if (!destination || !source)
        {
            return KEEL_FALSE;
        }
        try
        {
            *static_cast<Plain*>(destination) = *static_cast<const Plain*>(source);
            return KEEL_TRUE;
        }
        catch (...)
        {
            return KEEL_FALSE;
        }
    }

    static void Destroy(void* value) noexcept
    {
        if (value)
        {
            std::destroy_at(static_cast<Plain*>(value));
        }
    }

    static inline constexpr KeelHookObject value{
        sizeof(KeelHookObject),
        sizeof(Plain),
        alignof(Plain),
        0,
        ObjectIdentity<Plain>(),
        &DefaultConstruct,
        &CopyConstruct,
        &CopyAssign,
        &Destroy
    };
};

template <typename Signature>
struct Prototype;

template <typename Return, typename... Arguments>
struct Prototype<Return(Arguments...)>
{
    static_assert(!std::is_rvalue_reference_v<Return>);
    static_assert((!std::is_rvalue_reference_v<Arguments> && ...));
    static_assert(sizeof...(Arguments) <= KEELHOOK_MAX_ARGUMENTS);
    static inline constexpr std::array<KeelHookValueType, sizeof...(Arguments)> arguments{
        ValueTypeV<Arguments>...
    };
    static inline constexpr std::array<const KeelHookAggregate*, sizeof...(Arguments)> aggregates{
        AggregateDescriptor<Arguments>()...
    };
    static inline constexpr std::array<const KeelHookObject*, sizeof...(Arguments)> objects{
        ObjectDescriptor<Arguments>()...
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
        0,
        ObjectDescriptor<Return>(),
        objects.data()
    };
};

template <typename Signature>
struct MethodPrototype;

template <typename Return, typename... Arguments>
struct MethodPrototype<Return(Arguments...)>
{
    static_assert(!std::is_rvalue_reference_v<Return>);
    static_assert((!std::is_rvalue_reference_v<Arguments> && ...));
    static_assert(sizeof...(Arguments) + 1 <= KEELHOOK_MAX_ARGUMENTS);
    static inline constexpr std::array<KeelHookValueType, sizeof...(Arguments) + 1> arguments{
        KH_VALUE_POINTER,
        ValueTypeV<Arguments>...
    };
    static inline constexpr std::array<const KeelHookAggregate*, sizeof...(Arguments) + 1> aggregates{
        nullptr,
        AggregateDescriptor<Arguments>()...
    };
    static inline constexpr std::array<const KeelHookObject*, sizeof...(Arguments) + 1> objects{
        nullptr,
        ObjectDescriptor<Arguments>()...
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
        0,
        ObjectDescriptor<Return>(),
        objects.data()
    };
};

template <typename Signature>
struct VafmtPrototype;

template <typename Return, typename... Arguments>
struct VafmtPrototype<Return(Arguments...)>
{
    static_assert(!std::is_rvalue_reference_v<Return>);
    static_assert((!std::is_rvalue_reference_v<Arguments> && ...));
    static_assert(sizeof...(Arguments) + 1 <= KEELHOOK_MAX_ARGUMENTS);
    static inline constexpr std::array<KeelHookValueType, sizeof...(Arguments) + 1> arguments{
        ValueTypeV<Arguments>...,
        KH_VALUE_POINTER
    };
    static inline constexpr std::array<const KeelHookAggregate*, sizeof...(Arguments) + 1> aggregates{
        AggregateDescriptor<Arguments>()...,
        nullptr
    };
    static inline constexpr std::array<const KeelHookObject*, sizeof...(Arguments) + 1> objects{
        ObjectDescriptor<Arguments>()...,
        nullptr
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
        KH_PROTOTYPE_VAFMT,
        ObjectDescriptor<Return>(),
        objects.data()
    };
};

template <typename Signature>
struct MethodVafmtPrototype;

template <typename Return, typename... Arguments>
struct MethodVafmtPrototype<Return(Arguments...)>
{
    static_assert(!std::is_rvalue_reference_v<Return>);
    static_assert((!std::is_rvalue_reference_v<Arguments> && ...));
    static_assert(sizeof...(Arguments) + 2 <= KEELHOOK_MAX_ARGUMENTS);
    static inline constexpr std::array<KeelHookValueType, sizeof...(Arguments) + 2> arguments{
        KH_VALUE_POINTER,
        ValueTypeV<Arguments>...,
        KH_VALUE_POINTER
    };
    static inline constexpr std::array<const KeelHookAggregate*, sizeof...(Arguments) + 2> aggregates{
        nullptr,
        AggregateDescriptor<Arguments>()...,
        nullptr
    };
    static inline constexpr std::array<const KeelHookObject*, sizeof...(Arguments) + 2> objects{
        nullptr,
        ObjectDescriptor<Arguments>()...,
        nullptr
    };
    static inline constexpr KeelHookPrototype value{
        sizeof(KeelHookPrototype),
        KH_CALL_NATIVE,
        ValueTypeV<Return>,
        static_cast<std::uint32_t>(sizeof...(Arguments) + 2),
        arguments.data(),
        AggregateDescriptor<Return>(),
        aggregates.data(),
        static_cast<std::uint32_t>(sizeof...(Arguments) + 2),
        KH_PROTOTYPE_VAFMT,
        ObjectDescriptor<Return>(),
        objects.data()
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
    if constexpr (ManagedObject<Plain> && !AdaptedValue<Plain>)
    {
        return value.scalar.aggregate.data &&
            value.scalar.aggregate.size == sizeof(Plain) &&
            value.scalar.aggregate.reserved == KH_VALUE_OBJECT_CONSTRUCTED;
    }
    else if constexpr (DescribedAggregate<Plain> && !AdaptedValue<Plain>)
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
    requires (!std::is_reference_v<Type>)
Type Read(const KeelHookValue& value)
{
    using Plain = std::remove_cv_t<Type>;
    if (!ValidValue<Plain>(value))
    {
        if constexpr (AdaptedValue<Plain>)
        {
            return ValueAdapter<Plain>::Fallback();
        }
        else
        {
            return Plain{};
        }
    }
    if constexpr (AdaptedValue<Plain>)
    {
        return ValueAdapter<Plain>::Read(value);
    }
    else if constexpr (ManagedObject<Plain>)
    {
        return *static_cast<const Plain*>(value.scalar.aggregate.data);
    }
    else if constexpr (DescribedAggregate<Plain>)
    {
        Plain result{};
        std::memcpy(&result, value.scalar.aggregate.data, sizeof(result));
        return result;
    }
    else if constexpr (std::is_enum_v<Plain>)
    {
        using Underlying = std::underlying_type_t<Plain>;
        return static_cast<Plain>(Read<Underlying>(value));
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
    else if constexpr (std::is_integral_v<Plain>)
    {
        if constexpr (sizeof(Plain) == 1)
        {
            if constexpr (std::is_signed_v<Plain>)
            {
                return static_cast<Plain>(value.scalar.int8);
            }
            else
            {
                return static_cast<Plain>(value.scalar.uint8);
            }
        }
        else if constexpr (sizeof(Plain) == 2)
        {
            if constexpr (std::is_signed_v<Plain>)
            {
                return static_cast<Plain>(value.scalar.int16);
            }
            else
            {
                return static_cast<Plain>(value.scalar.uint16);
            }
        }
        else if constexpr (sizeof(Plain) == 4)
        {
            if constexpr (std::is_signed_v<Plain>)
            {
                return static_cast<Plain>(value.scalar.int32);
            }
            else
            {
                return static_cast<Plain>(value.scalar.uint32);
            }
        }
        else
        {
            static_assert(sizeof(Plain) == 8);
            if constexpr (std::is_signed_v<Plain>)
            {
                return static_cast<Plain>(value.scalar.int64);
            }
            else
            {
                return static_cast<Plain>(value.scalar.uint64);
            }
        }
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
    requires (!std::is_reference_v<Type>)
bool Write(KeelHookValue& value, const Type& input) noexcept
{
    using Plain = std::remove_cv_t<Type>;
    if constexpr (ManagedObject<Plain> && !AdaptedValue<Plain>)
    {
        if (value.type != KH_VALUE_AGGREGATE || value.reserved != 0 ||
            !value.scalar.aggregate.data || value.scalar.aggregate.size != sizeof(Plain) ||
            (value.scalar.aggregate.reserved != 0 &&
                value.scalar.aggregate.reserved != KH_VALUE_OBJECT_CONSTRUCTED))
        {
            return false;
        }
    }
    else if (!ValidValue<Plain>(value))
    {
        return false;
    }
    if constexpr (AdaptedValue<Plain>)
    {
        return ValueAdapter<Plain>::Write(value, input);
    }
    else if constexpr (ManagedObject<Plain>)
    {
        try
        {
            auto* destination = static_cast<Plain*>(value.scalar.aggregate.data);
            if (value.scalar.aggregate.reserved == KH_VALUE_OBJECT_CONSTRUCTED)
            {
                *destination = input;
            }
            else
            {
                std::construct_at(destination, input);
                value.scalar.aggregate.reserved = KH_VALUE_OBJECT_CONSTRUCTED;
            }
        }
        catch (...)
        {
            return false;
        }
    }
    else if constexpr (DescribedAggregate<Plain>)
    {
        std::memcpy(value.scalar.aggregate.data, &input, sizeof(input));
    }
    else if constexpr (std::is_enum_v<Plain>)
    {
        using Underlying = std::underlying_type_t<Plain>;
        if (!Write(value, static_cast<Underlying>(input)))
        {
            return false;
        }
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
    else if constexpr (std::is_integral_v<Plain>)
    {
        if constexpr (sizeof(Plain) == 1)
        {
            if constexpr (std::is_signed_v<Plain>)
            {
                value.scalar.int8 = static_cast<std::int8_t>(input);
            }
            else
            {
                value.scalar.uint8 = static_cast<std::uint8_t>(input);
            }
        }
        else if constexpr (sizeof(Plain) == 2)
        {
            if constexpr (std::is_signed_v<Plain>)
            {
                value.scalar.int16 = static_cast<std::int16_t>(input);
            }
            else
            {
                value.scalar.uint16 = static_cast<std::uint16_t>(input);
            }
        }
        else if constexpr (sizeof(Plain) == 4)
        {
            if constexpr (std::is_signed_v<Plain>)
            {
                value.scalar.int32 = static_cast<std::int32_t>(input);
            }
            else
            {
                value.scalar.uint32 = static_cast<std::uint32_t>(input);
            }
        }
        else
        {
            static_assert(sizeof(Plain) == 8);
            if constexpr (std::is_signed_v<Plain>)
            {
                value.scalar.int64 = static_cast<std::int64_t>(input);
            }
            else
            {
                value.scalar.uint64 = static_cast<std::uint64_t>(input);
            }
        }
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

template <typename Type>
    requires std::is_lvalue_reference_v<Type>
std::add_pointer_t<std::remove_reference_t<Type>> ReadReference(
    const KeelHookValue& value) noexcept
{
    using Pointer = std::add_pointer_t<std::remove_reference_t<Type>>;
    return ValidValue<Type>(value) ? Read<Pointer>(value) : nullptr;
}

template <typename Type>
    requires std::is_lvalue_reference_v<Type>
bool WriteReference(
    KeelHookValue& value,
    std::remove_reference_t<Type>& input) noexcept
{
    using Pointer = std::add_pointer_t<std::remove_reference_t<Type>>;
    return Write(value, static_cast<Pointer>(std::addressof(input)));
}

enum class Action : std::uint32_t
{
    Continue = KH_ACTION_CONTINUE,
    Override = KH_ACTION_OVERRIDE,
    Supersede = KH_ACTION_SUPERSEDE
};

enum class Phase : std::uint32_t
{
    Pre = KH_PHASE_PRE,
    Post = KH_PHASE_POST,
    Both = KH_PHASE_BOTH
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

    bool Recalled() const noexcept
    {
        return *this && (frame_->flags & KH_FRAME_RECALLED) != 0;
    }

    template <typename Type>
        requires (!std::is_reference_v<Type>)
    std::optional<std::remove_cvref_t<Type>> Argument(std::size_t index) const
    {
        using Plain = std::remove_cvref_t<Type>;
        if (!*this || index >= frame_->argument_count ||
            !ValidValue<Plain>(frame_->arguments[index]))
        {
            return std::nullopt;
        }
        return Read<Plain>(frame_->arguments[index]);
    }

    template <typename Type>
        requires (!std::is_reference_v<Type>)
    bool SetArgument(std::size_t index, const Type& value) noexcept
    {
        return *this && index < frame_->argument_count &&
            Write(frame_->arguments[index], value);
    }

    template <typename Type>
        requires (!std::is_reference_v<Type>)
    std::optional<std::remove_cvref_t<Type>> Result() const
    {
        using Plain = std::remove_cvref_t<Type>;
        static_assert(!std::is_void_v<Plain>);
        if (!*this || !ValidValue<Plain>(frame_->result))
        {
            return std::nullopt;
        }
        return Read<Plain>(frame_->result);
    }

    template <typename Type>
        requires std::is_lvalue_reference_v<Type>
    std::add_pointer_t<std::remove_reference_t<Type>> Result() const noexcept
    {
        static_assert(!std::is_void_v<std::remove_reference_t<Type>>);
        return *this ? ReadReference<Type>(frame_->result) : nullptr;
    }

    template <typename Type>
        requires (!std::is_reference_v<Type>)
    bool SetResult(const Type& value) noexcept
    {
        return *this && Write(frame_->result, value);
    }

    template <typename Type>
        requires std::is_lvalue_reference_v<Type>
    bool SetResult(std::remove_reference_t<Type>& value) noexcept
    {
        return *this && WriteReference<Type>(frame_->result, value);
    }

    KeelHookFrame* Raw() const noexcept
    {
        return frame_;
    }

private:
    KeelHookFrame* frame_{};
};

template <typename Return>
class Call;

namespace detail
{

template <typename Signature, bool Method, auto Callback, typename Owner>
struct TypedCallback;

template <typename Signature, bool Method, typename CallbackMethod>
struct CallbackDispatch;

template <typename Signature, bool Method, typename CallbackMethod, typename Owner>
struct BoundTypedCallback;

template <typename Type>
struct MemberFunctionTraits;

template <typename Return, typename Class, typename... Arguments>
struct MemberFunctionTraitsBase
{
    using ClassType = Class;
    using ReturnType = Return;
    using Signature = Return(Arguments...);
};

template <typename Return, typename Class, typename... Arguments>
struct MemberFunctionTraits<Return (Class::*)(Arguments...)>
    : MemberFunctionTraitsBase<Return, Class, Arguments...>
{
};

template <typename Return, typename Class, typename... Arguments>
struct MemberFunctionTraits<Return (Class::*)(Arguments...) const>
    : MemberFunctionTraitsBase<Return, Class, Arguments...>
{
};

template <typename Return, typename Class, typename... Arguments>
struct MemberFunctionTraits<Return (Class::*)(Arguments...) noexcept>
    : MemberFunctionTraitsBase<Return, Class, Arguments...>
{
};

template <typename Return, typename Class, typename... Arguments>
struct MemberFunctionTraits<Return (Class::*)(Arguments...) const noexcept>
    : MemberFunctionTraitsBase<Return, Class, Arguments...>
{
};

template <typename Target, typename Callback>
consteval bool CompatibleCallbackArgument()
{
    if constexpr (std::is_lvalue_reference_v<Target>)
    {
        return std::is_same_v<Target, Callback>;
    }
    else
    {
        return !std::is_rvalue_reference_v<Callback> &&
            std::is_same_v<
                std::remove_cvref_t<Callback>,
                std::remove_cv_t<Target>>;
    }
}

template <typename TargetSignature, typename CallbackSignature>
struct CallbackCompatibility;

template <typename Return, typename... Arguments, typename CallbackReturn,
    typename... CallbackArguments>
struct CallbackCompatibility<
    Return(Arguments...),
    CallbackReturn(CallbackArguments...)>
{
private:
    using CallbackTuple = std::tuple<CallbackArguments...>;

    static constexpr bool valid_return =
        std::is_void_v<CallbackReturn> ||
        std::is_same_v<CallbackReturn, PluginResult> ||
        std::is_same_v<CallbackReturn, Action> ||
        std::is_same_v<CallbackReturn, KeelHookAction>;

    template <std::size_t Offset, std::size_t... Indexes>
    static consteval bool CompatibleArguments(std::index_sequence<Indexes...>)
    {
        return (CompatibleCallbackArgument<
            Arguments,
            std::tuple_element_t<Offset + Indexes, CallbackTuple>>() && ...);
    }

    static consteval bool WithoutCall()
    {
        if constexpr (sizeof...(CallbackArguments) != sizeof...(Arguments))
        {
            return false;
        }
        else
        {
            return valid_return && CompatibleArguments<0>(
                std::index_sequence_for<Arguments...>{});
        }
    }

    static consteval bool WithCall()
    {
        if constexpr (sizeof...(CallbackArguments) != sizeof...(Arguments) + 1)
        {
            return false;
        }
        else
        {
            using First = std::tuple_element_t<0, CallbackTuple>;
            return valid_return && std::is_lvalue_reference_v<First> &&
                std::is_same_v<std::remove_cvref_t<First>, Call<Return>> &&
                CompatibleArguments<1>(std::index_sequence_for<Arguments...>{});
        }
    }

public:
    static constexpr bool with_call = WithCall();
    static constexpr bool without_call = WithoutCall();
    static constexpr bool value = with_call || without_call;
};

template <typename Type>
using ArgumentStorage = std::conditional_t<
    std::is_lvalue_reference_v<Type>,
    std::add_pointer_t<std::remove_reference_t<Type>>,
    std::remove_cv_t<Type>>;

template <typename Type>
bool ValidArgument(const KeelHookValue& value)
{
    if constexpr (std::is_lvalue_reference_v<Type>)
    {
        using Pointer = std::add_pointer_t<std::remove_reference_t<Type>>;
        return ValidValue<Pointer>(value) && Read<Pointer>(value);
    }
    else
    {
        return ValidValue<std::remove_cv_t<Type>>(value);
    }
}

template <typename Type>
bool ValidResultStorage(const KeelHookValue& value)
{
    using Plain = std::remove_cv_t<Type>;
    if constexpr (ManagedObject<Plain> && !AdaptedValue<Plain>)
    {
        return value.type == KH_VALUE_AGGREGATE && value.reserved == 0 &&
            value.scalar.aggregate.data && value.scalar.aggregate.size == sizeof(Plain) &&
            (value.scalar.aggregate.reserved == 0 ||
                value.scalar.aggregate.reserved == KH_VALUE_OBJECT_CONSTRUCTED);
    }
    else
    {
        return ValidValue<Type>(value);
    }
}

template <typename Type>
ArgumentStorage<Type> ReadArgument(const KeelHookValue& value)
{
    if constexpr (std::is_lvalue_reference_v<Type>)
    {
        using Pointer = std::add_pointer_t<std::remove_reference_t<Type>>;
        return Read<Pointer>(value);
    }
    else
    {
        return Read<std::remove_cv_t<Type>>(value);
    }
}

template <typename Type>
decltype(auto) ExposeArgument(ArgumentStorage<Type>& value)
{
    if constexpr (std::is_lvalue_reference_v<Type>)
    {
        return static_cast<Type>(*value);
    }
    else
    {
        return (value);
    }
}

template <typename Type>
bool WriteArgument(KeelHookValue& destination, const ArgumentStorage<Type>& value)
{
    if constexpr (std::is_lvalue_reference_v<Type>)
    {
        return true;
    }
    else
    {
        return Write(destination, value);
    }
}

}

template <typename Return>
class Call final
{
public:
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;
    Call(Call&&) = delete;
    Call& operator=(Call&&) = delete;

    Phase CurrentPhase() const noexcept
    {
        return frame_.Phase() == KH_PHASE_POST ? Phase::Post : Phase::Pre;
    }

    bool OriginalCalled() const noexcept
    {
        return frame_.OriginalCalled();
    }

    KeelHookTargetHandle TargetHandle() const noexcept
    {
        return frame_.TargetHandle();
    }

    KeelHookFrame* Raw() const noexcept
    {
        return frame_.Raw();
    }

    template <typename Class>
    Class* Instance() const noexcept
    {
        return static_cast<Class*>(instance_);
    }

    template <typename Value = Return>
        requires (std::is_same_v<Value, Return> && !std::is_void_v<Value> &&
            !std::is_reference_v<Value>)
    std::optional<std::remove_cv_t<Value>> Result() const
    {
        return frame_.template Result<std::remove_cv_t<Value>>();
    }

    template <typename Value = Return>
        requires (std::is_same_v<Value, Return> && !std::is_void_v<Value> &&
            !std::is_reference_v<Value>)
    bool SetResult(const std::remove_cv_t<Value>& value) noexcept
    {
        return frame_.SetResult(value);
    }

    template <typename Value = Return>
        requires (std::is_same_v<Value, Return> && std::is_lvalue_reference_v<Value>)
    std::add_pointer_t<std::remove_reference_t<Value>> Result() const noexcept
    {
        return frame_.template Result<Value>();
    }

    template <typename Value = Return>
        requires (std::is_same_v<Value, Return> && std::is_lvalue_reference_v<Value>)
    bool SetResult(std::remove_reference_t<Value>& value) noexcept
    {
        return frame_.template SetResult<Value>(value);
    }

private:
    template <typename Signature, bool Method, typename CallbackMethod>
    friend struct detail::CallbackDispatch;

    template <typename Signature, bool Method, auto Callback, typename Owner>
    friend struct detail::TypedCallback;

    Call(KeelHookFrame* frame, void* instance) noexcept
        : frame_(frame), instance_(instance)
    {
    }

    Frame frame_;
    void* instance_{};
};

namespace detail
{

template <typename Return, typename... Arguments, bool Method, typename CallbackMethod>
struct CallbackDispatch<Return(Arguments...), Method, CallbackMethod>
{
    static_assert(!std::is_rvalue_reference_v<Return>);
    static_assert((!std::is_rvalue_reference_v<Arguments> && ...));

    template <typename Type>
    using ExposedArgument = decltype(ExposeArgument<Type>(
        std::declval<ArgumentStorage<Type>&>()));

    using Compatibility = CallbackCompatibility<
        Return(Arguments...),
        typename MemberFunctionTraits<CallbackMethod>::Signature>;
    static constexpr bool with_call = Compatibility::with_call;

    static_assert(
        Compatibility::value,
        "typed KeelHook callback does not match the target method arguments");

    template <typename Owner, typename... Parameters>
    static KeelHookAction Invoke(
        Owner& owner,
        CallbackMethod callback,
        Parameters&&... parameters)
    {
        using Result = std::invoke_result_t<
            CallbackMethod, Owner&, Parameters...>;
        if constexpr (std::is_void_v<Result>)
        {
            std::invoke(
                callback,
                owner,
                std::forward<Parameters>(parameters)...);
            return KH_ACTION_CONTINUE;
        }
        else
        {
            static_assert(
                std::is_same_v<Result, PluginResult> ||
                    std::is_same_v<Result, Action> ||
                    std::is_same_v<Result, KeelHookAction>,
                "typed KeelHook callbacks must return void or PluginResult");
            const Result result = std::invoke(
                callback,
                owner,
                std::forward<Parameters>(parameters)...);
            if constexpr (
                std::is_same_v<Result, PluginResult> ||
                std::is_same_v<Result, Action>)
            {
                return static_cast<KeelHookAction>(result);
            }
            else
            {
                return result;
            }
        }
    }

    template <std::size_t... Indexes>
    static bool ValidArguments(
        const KeelHookFrame& frame,
        std::size_t offset,
        std::index_sequence<Indexes...>)
    {
        return (ValidArgument<Arguments>(frame.arguments[offset + Indexes]) && ...);
    }

    template <typename Owner, std::size_t... Indexes>
    static KeelHookAction DispatchArguments(
        KeelHookFrame& frame,
        Owner& owner,
        CallbackMethod callback,
        void* instance,
        std::size_t offset,
        std::index_sequence<Indexes...>)
    {
        std::tuple<ArgumentStorage<Arguments>...> arguments{
            ReadArgument<Arguments>(frame.arguments[offset + Indexes])...
        };
        Call<Return> call(&frame, instance);
        KeelHookAction action{};
        if constexpr (with_call)
        {
            action = Invoke(
                owner,
                callback,
                call,
                ExposeArgument<Arguments>(std::get<Indexes>(arguments))...);
        }
        else
        {
            action = Invoke(
                owner,
                callback,
                ExposeArgument<Arguments>(std::get<Indexes>(arguments))...);
        }
        const bool written = (WriteArgument<Arguments>(
            frame.arguments[offset + Indexes],
            std::get<Indexes>(arguments)) && ...);
        return written ? action : KH_ACTION_CONTINUE;
    }

    template <typename Owner>
    static KeelHookAction Dispatch(
        KeelHookFrame* frame,
        Owner* owner,
        CallbackMethod callback)
    {
        constexpr std::size_t argument_count = sizeof...(Arguments) + (Method ? 1 : 0);
        if (!frame || !owner || frame->size != sizeof(KeelHookFrame) ||
            (frame->phase != KH_PHASE_PRE && frame->phase != KH_PHASE_POST) ||
            frame->argument_count != argument_count ||
            (argument_count != 0 && !frame->arguments) ||
            (frame->flags & ~(KH_FRAME_ORIGINAL_CALLED | KH_FRAME_RECALLED)) != 0 ||
            !ValidResultStorage<Return>(frame->result))
        {
            return KH_ACTION_CONTINUE;
        }
        void* instance{};
        constexpr std::size_t offset = Method ? 1 : 0;
        if constexpr (Method)
        {
            if (!ValidValue<void*>(frame->arguments[0]) ||
                !(instance = Read<void*>(frame->arguments[0])))
            {
                return KH_ACTION_CONTINUE;
            }
        }
        if (!ValidArguments(
                *frame,
                offset,
                std::index_sequence_for<Arguments...>{}))
        {
            return KH_ACTION_CONTINUE;
        }
        return DispatchArguments(
            *frame,
            *owner,
            callback,
            instance,
            offset,
            std::index_sequence_for<Arguments...>{});
    }
};

template <typename Signature, bool Method, auto Callback, typename Owner>
struct TypedCallback
{
    static KeelHookAction Dispatch(KeelHookFrame* frame, void* user_data)
    {
        return CallbackDispatch<
            Signature,
            Method,
            decltype(Callback)>::Dispatch(
                frame,
                static_cast<Owner*>(user_data),
                Callback);
    }
};

template <typename Signature, bool Method, typename CallbackMethod, typename Owner>
struct BoundTypedCallback
{
    BoundTypedCallback(Owner& value_owner, CallbackMethod value_callback) noexcept
        : owner(&value_owner), callback(value_callback)
    {
    }

    static KeelHookAction Dispatch(KeelHookFrame* frame, void* user_data)
    {
        auto* binding = static_cast<BoundTypedCallback*>(user_data);
        if (!binding)
        {
            return KH_ACTION_CONTINUE;
        }
        return CallbackDispatch<Signature, Method, CallbackMethod>::Dispatch(
            frame,
            binding->owner,
            binding->callback);
    }

    Owner* owner{};
    CallbackMethod callback{};
};

struct VirtualMethodInfo
{
    std::uint32_t index{};
    std::int64_t this_adjustment{};
    std::int64_t vtable_offset{};
};

template <typename Pointer>
std::optional<VirtualMethodInfo> VirtualMethod(Pointer method) noexcept
{
    static_assert(std::is_member_function_pointer_v<Pointer>);
#if defined(_MSC_VER) && defined(_M_X64)
    if constexpr (sizeof(Pointer) != sizeof(void*) &&
        sizeof(Pointer) != sizeof(void*) * 2)
    {
        return std::nullopt;
    }
    std::array<std::byte, sizeof(Pointer)> representation{};
    std::memcpy(representation.data(), &method, sizeof(method));
    std::uintptr_t thunk{};
    std::memcpy(&thunk, representation.data(), sizeof(thunk));
    if (!thunk)
    {
        return std::nullopt;
    }
    if constexpr (sizeof(Pointer) == sizeof(void*) * 2)
    {
        std::int32_t virtual_offset{};
        std::memcpy(
            &virtual_offset,
            representation.data() + sizeof(void*) + sizeof(std::int32_t),
            sizeof(virtual_offset));
        if (virtual_offset != 0)
        {
            return std::nullopt;
        }
    }
    const auto* code = reinterpret_cast<const std::uint8_t*>(thunk);
    if (code[0] == 0xE9)
    {
        std::int32_t displacement{};
        std::memcpy(&displacement, code + 1, sizeof(displacement));
        const auto base = reinterpret_cast<std::uintptr_t>(code + 5);
        if ((displacement > 0 && static_cast<std::uintptr_t>(displacement) >
                (std::numeric_limits<std::uintptr_t>::max)() - base) ||
            (displacement < 0 && static_cast<std::uintptr_t>(-(
                static_cast<std::int64_t>(displacement))) > base))
        {
            return std::nullopt;
        }
        code = reinterpret_cast<const std::uint8_t*>(
            displacement >= 0
                ? base + static_cast<std::uintptr_t>(displacement)
                : base - static_cast<std::uintptr_t>(-(
                    static_cast<std::int64_t>(displacement))));
    }
    if (code[0] != 0x48 || code[1] != 0x8B || code[2] != 0x01 ||
        code[3] != 0xFF)
    {
        return std::nullopt;
    }
    std::uint32_t byte_offset{};
    if (code[4] == 0x20)
    {
        byte_offset = 0;
    }
    else if (code[4] == 0x60)
    {
        byte_offset = code[5];
    }
    else if (code[4] == 0xA0)
    {
        std::int32_t displacement{};
        std::memcpy(&displacement, code + 5, sizeof(displacement));
        if (displacement < 0)
        {
            return std::nullopt;
        }
        byte_offset = static_cast<std::uint32_t>(displacement);
    }
    else
    {
        return std::nullopt;
    }
    if (byte_offset % sizeof(void*) != 0)
    {
        return std::nullopt;
    }
    std::int64_t adjustment{};
    if constexpr (sizeof(Pointer) == sizeof(void*) * 2)
    {
        std::int32_t encoded{};
        std::memcpy(&encoded, representation.data() + sizeof(void*), sizeof(encoded));
        adjustment = encoded;
    }
    return VirtualMethodInfo{
        static_cast<std::uint32_t>(byte_offset / sizeof(void*)),
        adjustment,
        0
    };
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    struct Representation
    {
        std::intptr_t function;
        std::intptr_t adjustment;
    };
    if constexpr (sizeof(Pointer) != sizeof(Representation))
    {
        return std::nullopt;
    }
    Representation representation{};
    std::memcpy(&representation, &method, sizeof(method));
    if (representation.function <= 0 ||
        (representation.function & 1) == 0)
    {
        return std::nullopt;
    }
    const auto byte_offset = static_cast<std::uintptr_t>(representation.function - 1);
    const auto index = byte_offset / sizeof(void*);
    if (byte_offset % sizeof(void*) != 0 ||
        index > (std::numeric_limits<std::uint32_t>::max)())
    {
        return std::nullopt;
    }
    return VirtualMethodInfo{
        static_cast<std::uint32_t>(index),
        static_cast<std::int64_t>(representation.adjustment),
        0
    };
#else
    return std::nullopt;
#endif
}

}

using VirtualMethodInfo = detail::VirtualMethodInfo;

template <auto Method>
using MethodClass = typename detail::MemberFunctionTraits<decltype(Method)>::ClassType;

template <auto Method>
using MethodSignature = typename detail::MemberFunctionTraits<decltype(Method)>::Signature;

template <typename Method>
using MethodClassOf = typename detail::MemberFunctionTraits<
    std::remove_cvref_t<Method>>::ClassType;

template <typename Method>
using MethodSignatureOf = typename detail::MemberFunctionTraits<
    std::remove_cvref_t<Method>>::Signature;

template <typename Signature, auto Callback>
concept CompatibleCallback = detail::CallbackCompatibility<
    Signature,
    typename detail::MemberFunctionTraits<decltype(Callback)>::Signature>::value;

template <auto TargetMethod, auto CallbackMethod>
concept CompatibleMethodCallback =
    CompatibleCallback<MethodSignature<TargetMethod>, CallbackMethod>;

template <typename TargetMethod, typename CallbackMethod>
concept CompatibleMethods = detail::CallbackCompatibility<
    MethodSignatureOf<TargetMethod>,
    MethodSignatureOf<CallbackMethod>>::value;

template <auto Method>
std::optional<detail::VirtualMethodInfo> VirtualInfo() noexcept
{
    return detail::VirtualMethod(Method);
}

template <typename Method>
std::optional<detail::VirtualMethodInfo> VirtualInfo(Method method) noexcept
{
    return detail::VirtualMethod(method);
}

template <auto Method>
std::optional<std::uint32_t> VirtualIndex() noexcept
{
    const auto method = VirtualInfo<Method>();
    return method ? std::optional<std::uint32_t>(method->index) : std::nullopt;
}

template <typename Method>
std::optional<std::uint32_t> VirtualIndex(Method method) noexcept
{
    const auto info = VirtualInfo(method);
    return info ? std::optional<std::uint32_t>(info->index) : std::nullopt;
}

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

    static TargetSpec Profile(const char* name) noexcept
    {
        TargetSpec result;
        result.value_.source = KH_TARGET_PROFILE;
        result.value_.mechanism = KH_MECHANISM_DETOUR;
        result.value_.symbol = name;
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
        const char* profile = nullptr,
        std::int64_t this_adjustment = 0,
        std::int64_t vtable_offset = 0) noexcept
    {
        VirtualTargetSpec result;
        result.value_.mechanism = KH_MECHANISM_VIRTUAL;
        result.value_.instance = instance;
        result.value_.index = index;
        result.value_.profile = profile;
        result.value_.this_adjustment = this_adjustment;
        result.value_.vtable_offset = vtable_offset;
        return result;
    }

    static VirtualTargetSpec Instance(
        void* instance,
        std::uint32_t index,
        std::uint32_t table_size,
        const char* profile = nullptr,
        std::int64_t this_adjustment = 0,
        std::int64_t vtable_offset = 0) noexcept
    {
        VirtualTargetSpec result;
        result.value_.mechanism = KH_MECHANISM_VIRTUAL_INSTANCE;
        result.value_.instance = instance;
        result.value_.index = index;
        result.value_.table_size = table_size;
        result.value_.profile = profile;
        result.value_.this_adjustment = this_adjustment;
        result.value_.vtable_offset = vtable_offset;
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
    KeelResult ReleaseReferenceLocked() noexcept
    {
        if (references == 0)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        --references;
        if (references != 0)
        {
            return KEEL_RESULT_OK;
        }
        KeelResult result = KEEL_RESULT_NOT_READY;
        if (context &&
            context->accepting_resources.load(std::memory_order_acquire) &&
            context->api && api && api->release_target && handle)
        {
            result = api->release_target(context->plugin, handle);
        }
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            if (context)
            {
                const auto iterator = context->keelhook_targets.find(handle);
                if (iterator != context->keelhook_targets.end())
                {
                    const auto registered = iterator->second.lock();
                    if (!registered || registered.get() == this)
                    {
                        context->keelhook_targets.erase(iterator);
                    }
                }
            }
            api = nullptr;
            handle = 0;
            context.reset();
        }
        else
        {
            ++references;
        }
        return result;
    }

    std::shared_ptr<keels2::detail::ContextState> context;
    const KeelHookApi* api{};
    KeelHookTargetHandle handle{};
    std::size_t references{};
};

}

class Service;
class Callback;
class Hook;

class Target final
{
public:
    Target() = default;
    ~Target()
    {
        static_cast<void>(Reset());
    }
    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;
    Target(Target&& other) noexcept
        : state_(std::move(other.state_))
    {
    }
    Target& operator=(Target&& other) noexcept
    {
        if (this != &other)
        {
            state_.swap(other.state_);
        }
        return *this;
    }

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
        const auto context = state_->context;
        if (!context)
        {
            state_.reset();
            return KEEL_RESULT_NOT_READY;
        }
        std::scoped_lock lock(context->keelhook_targets_mutex);
        const KeelResult result = state_->ReleaseReferenceLocked();
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
    friend class Hook;
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
            target_.swap(other.target_);
            binding_.swap(other.binding_);
            std::swap(handle_, other.handle_);
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

    KeelResult SetEnabled(bool enabled) noexcept
    {
        const std::shared_ptr<detail::TargetState> target = target_;
        const std::shared_ptr<keels2::detail::ContextState> context =
            target ? target->context : nullptr;
        if (!handle_ || !target || !context || !target->api ||
            !target->api->set_callback_enabled)
        {
            return KEEL_RESULT_NOT_READY;
        }
        return target->api->set_callback_enabled(
            context->plugin,
            handle_,
            enabled ? KEEL_TRUE : KEEL_FALSE);
    }

    KeelResult Reset() noexcept
    {
        if (!target_)
        {
            return KEEL_RESULT_OK;
        }
        const auto context = target_->context;
        if (!context)
        {
            Clear();
            return KEEL_RESULT_NOT_READY;
        }
        KeelResult callback_result = KEEL_RESULT_OK;
        if (handle_)
        {
            if (!context->accepting_resources.load(std::memory_order_acquire) ||
                !context->api || !target_->api || !target_->api->remove_callback)
            {
                callback_result = KEEL_RESULT_NOT_READY;
            }
            else
            {
                callback_result = target_->api->remove_callback(
                    context->plugin,
                    handle_);
            }
            if (callback_result != KEEL_RESULT_OK &&
                callback_result != KEEL_RESULT_NOT_FOUND &&
                callback_result != KEEL_RESULT_NOT_READY)
            {
                return callback_result;
            }
            handle_ = 0;
        }
        std::scoped_lock lock(context->keelhook_targets_mutex);
        const KeelResult release_result = target_->ReleaseReferenceLocked();
        if (release_result == KEEL_RESULT_OK || release_result == KEEL_RESULT_NOT_FOUND ||
            release_result == KEEL_RESULT_NOT_READY)
        {
            target_.reset();
            binding_.reset();
        }
        return release_result == KEEL_RESULT_OK ? callback_result : release_result;
    }

private:
    friend class Service;
    friend class Hook;

    void Adopt(
        std::shared_ptr<detail::TargetState> target,
        KeelHookCallbackHandle handle,
        std::shared_ptr<void> binding = {}) noexcept
    {
        target_ = std::move(target);
        handle_ = handle;
        binding_ = std::move(binding);
    }

    void Clear() noexcept
    {
        handle_ = 0;
        target_.reset();
        binding_.reset();
    }

    void MoveFrom(Callback& other) noexcept
    {
        target_ = std::move(other.target_);
        binding_ = std::move(other.binding_);
        handle_ = std::exchange(other.handle_, 0);
    }

    std::shared_ptr<detail::TargetState> target_;
    std::shared_ptr<void> binding_;
    KeelHookCallbackHandle handle_{};
};

class Hook final
{
public:
    Hook() = default;
    ~Hook()
    {
        static_cast<void>(Reset());
    }

    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;
    Hook(Hook&&) noexcept = default;
    Hook& operator=(Hook&&) noexcept = default;

    explicit operator bool() const noexcept
    {
        return target_ && callback_;
    }

    KeelHookTargetHandle TargetHandle() const noexcept
    {
        return target_.Handle();
    }

    KeelHookCallbackHandle CallbackHandle() const noexcept
    {
        return callback_.Handle();
    }

    KeelResult Reset() noexcept
    {
        const KeelResult callback_result = callback_.Reset();
        if (callback_result != KEEL_RESULT_OK &&
            callback_result != KEEL_RESULT_NOT_FOUND &&
            callback_result != KEEL_RESULT_NOT_READY)
        {
            return callback_result;
        }
        return target_.Reset();
    }

private:
    friend class Service;

    bool Empty() const noexcept
    {
        return !target_.state_ && !callback_.handle_;
    }

    Target target_;
    Callback callback_;
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
            !api->resolve_virtual_target || !api->call_original || !api->recall ||
            !api->set_callback_enabled)
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

    KeelResult CallOriginal(Frame& frame) const noexcept
    {
        return *this && frame
            ? api_->call_original(context_->plugin, frame.Raw())
            : KEEL_RESULT_NOT_READY;
    }

    template <typename Return>
    KeelResult CallOriginal(Call<Return>& call) const noexcept
    {
        return *this && call.Raw()
            ? api_->call_original(context_->plugin, call.Raw())
            : KEEL_RESULT_NOT_READY;
    }

    KeelResult Recall(Frame& frame) const noexcept
    {
        return *this && frame
            ? api_->recall(context_->plugin, frame.Raw())
            : KEEL_RESULT_NOT_READY;
    }

    template <typename Return>
    KeelResult Recall(Call<Return>& call) const noexcept
    {
        return *this && call.Raw()
            ? api_->recall(context_->plugin, call.Raw())
            : KEEL_RESULT_NOT_READY;
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
    KeelResult ResolveVafmt(const TargetSpec& spec, Target& output)
    {
        return ResolveTarget(spec, VafmtPrototype<Signature>::value, output);
    }

    template <typename Signature>
    KeelResult ResolveMethodVafmt(const TargetSpec& spec, Target& output)
    {
        TargetSpec method = spec;
        method.AsMethod();
        return ResolveTarget(method, MethodVafmtPrototype<Signature>::value, output);
    }

    template <typename Signature>
    KeelResult Resolve(const VirtualTargetSpec& spec, Target& output)
    {
        return ResolveVirtualTarget(spec, MethodPrototype<Signature>::value, output);
    }

    template <typename Signature>
    KeelResult ResolveVafmt(const VirtualTargetSpec& spec, Target& output)
    {
        return ResolveVirtualTarget(spec, MethodVafmtPrototype<Signature>::value, output);
    }

    template <auto Method>
    KeelResult Resolve(
        MethodClass<Method>* instance,
        const char* profile,
        Target& output)
    {
        static_assert(std::is_member_function_pointer_v<decltype(Method)>);
        const auto method = VirtualInfo<Method>();
        if (!method)
        {
            return KEEL_RESULT_UNSUPPORTED;
        }
        return Resolve<MethodSignature<Method>>(
            VirtualTargetSpec::Shared(
                instance,
                method->index,
                profile,
                method->this_adjustment,
                method->vtable_offset),
            output);
    }

    template <typename Method>
    KeelResult Resolve(
        MethodClassOf<Method>* instance,
        Method method,
        const char* profile,
        Target& output)
    {
        static_assert(std::is_member_function_pointer_v<Method>);
        const auto info = VirtualInfo(method);
        if (!info)
        {
            return KEEL_RESULT_UNSUPPORTED;
        }
        return Resolve<MethodSignatureOf<Method>>(
            VirtualTargetSpec::Shared(
                instance,
                info->index,
                profile,
                info->this_adjustment,
                info->vtable_offset),
            output);
    }

    KeelResult AddCallback(
        const Target& target,
        const KeelHookCallbackSpec& spec,
        Callback& output) const noexcept
    {
        return AddCallback(target, spec, output, {});
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
        static_assert(
            std::is_same_v<Result, PluginResult> ||
            std::is_same_v<Result, Action> ||
            std::is_same_v<Result, KeelHookAction>);
        const KeelHookCallbackSpec spec{
            sizeof(KeelHookCallbackSpec),
            phases,
            priority,
            0,
            [](KeelHookFrame* frame, void* user_data) -> KeelHookAction {
                Frame view(frame);
                if constexpr (
                    std::is_same_v<Result, PluginResult> ||
                    std::is_same_v<Result, Action>)
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

    template <typename Signature, auto Method, typename Owner>
    KeelResult AddCallback(
        const Target& target,
        Callback& output,
        Phase phase,
        std::int32_t priority,
        Owner& owner) const noexcept
    {
        static_assert(CompatibleCallback<Signature, Method>);
        static_assert(std::is_base_of_v<
            MethodClass<Method>,
            std::remove_cvref_t<Owner>>);
        const KeelHookCallbackSpec spec{
            sizeof(KeelHookCallbackSpec),
            static_cast<std::uint32_t>(phase),
            priority,
            0,
            &detail::TypedCallback<Signature, false, Method, Owner>::Dispatch,
            &owner
        };
        return AddCallback(target, spec, output);
    }

    template <auto TargetMethod, auto CallbackMethod, typename Owner>
    KeelResult AddMethodCallback(
        const Target& target,
        Callback& output,
        Phase phase,
        std::int32_t priority,
        Owner& owner) const noexcept
    {
        static_assert(std::is_member_function_pointer_v<decltype(TargetMethod)>);
        static_assert(CompatibleMethodCallback<TargetMethod, CallbackMethod>);
        static_assert(std::is_base_of_v<
            MethodClass<CallbackMethod>,
            std::remove_cvref_t<Owner>>);
        const KeelHookCallbackSpec spec{
            sizeof(KeelHookCallbackSpec),
            static_cast<std::uint32_t>(phase),
            priority,
            0,
            &detail::TypedCallback<
                MethodSignature<TargetMethod>,
                true,
                CallbackMethod,
                Owner>::Dispatch,
            &owner
        };
        return AddCallback(target, spec, output);
    }

    template <typename Signature, typename CallbackMethod, typename Owner>
    KeelResult AddMethodCallback(
        const Target& target,
        CallbackMethod callback_method,
        Callback& output,
        Phase phase,
        std::int32_t priority,
        Owner& owner) const
    {
        static_assert(std::is_member_function_pointer_v<CallbackMethod>);
        static_assert(detail::CallbackCompatibility<
            Signature,
            MethodSignatureOf<CallbackMethod>>::value);
        static_assert(std::is_base_of_v<
            MethodClassOf<CallbackMethod>,
            std::remove_cvref_t<Owner>>);
        using Binding = detail::BoundTypedCallback<
            Signature,
            true,
            CallbackMethod,
            Owner>;
        auto binding = std::make_shared<Binding>(owner, callback_method);
        const KeelHookCallbackSpec spec{
            sizeof(KeelHookCallbackSpec),
            static_cast<std::uint32_t>(phase),
            priority,
            0,
            &Binding::Dispatch,
            binding.get()
        };
        return AddCallback(
            target,
            spec,
            output,
            std::move(binding));
    }

    template <typename TargetMethod, typename CallbackMethod, typename Owner>
    KeelResult AddMethodCallback(
        const Target& target,
        TargetMethod,
        CallbackMethod callback_method,
        Callback& output,
        Phase phase,
        std::int32_t priority,
        Owner& owner) const
    {
        static_assert(std::is_member_function_pointer_v<TargetMethod>);
        static_assert(std::is_member_function_pointer_v<CallbackMethod>);
        static_assert(CompatibleMethods<TargetMethod, CallbackMethod>);
        static_assert(std::is_base_of_v<
            MethodClassOf<CallbackMethod>,
            std::remove_cvref_t<Owner>>);
        using Binding = detail::BoundTypedCallback<
            MethodSignatureOf<TargetMethod>,
            true,
            CallbackMethod,
            Owner>;
        auto binding = std::make_shared<Binding>(owner, callback_method);
        const KeelHookCallbackSpec spec{
            sizeof(KeelHookCallbackSpec),
            static_cast<std::uint32_t>(phase),
            priority,
            0,
            &Binding::Dispatch,
            binding.get()
        };
        return AddCallback(
            target,
            spec,
            output,
            std::move(binding));
    }

    template <auto TargetMethod, auto CallbackMethod, typename Owner>
    KeelResult AddVirtualHook(
        MethodClass<TargetMethod>* instance,
        const char* profile,
        Hook& output,
        Phase phase,
        std::int32_t priority,
        Owner& owner)
    {
        if (!output.Empty())
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        Hook hook;
        KeelResult result = Resolve<TargetMethod>(
            instance,
            profile,
            hook.target_);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        result = AddMethodCallback<TargetMethod, CallbackMethod>(
            hook.target_,
            hook.callback_,
            phase,
            priority,
            owner);
        if (result != KEEL_RESULT_OK)
        {
            static_cast<void>(hook.target_.Reset());
            return result;
        }
        output = std::move(hook);
        return KEEL_RESULT_OK;
    }

    template <typename TargetMethod, typename CallbackMethod, typename Owner>
    KeelResult AddVirtualHook(
        MethodClassOf<TargetMethod>* instance,
        TargetMethod target_method,
        CallbackMethod callback_method,
        const char* profile,
        Hook& output,
        Phase phase,
        std::int32_t priority,
        Owner& owner)
    {
        if (!output.Empty())
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        Hook hook;
        KeelResult result = Resolve(
            instance,
            target_method,
            profile,
            hook.target_);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        result = AddMethodCallback(
            hook.target_,
            target_method,
            callback_method,
            hook.callback_,
            phase,
            priority,
            owner);
        if (result != KEEL_RESULT_OK)
        {
            static_cast<void>(hook.target_.Reset());
            return result;
        }
        output = std::move(hook);
        return KEEL_RESULT_OK;
    }

    template <typename Signature, typename CallbackMethod, typename Owner>
    KeelResult AddMethodHook(
        const TargetSpec& spec,
        CallbackMethod callback_method,
        Hook& output,
        Phase phase,
        std::int32_t priority,
        Owner& owner)
    {
        if (!output.Empty())
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        Hook hook;
        KeelResult result = ResolveMethod<Signature>(spec, hook.target_);
        if (result != KEEL_RESULT_OK)
        {
            return result;
        }
        result = AddMethodCallback<Signature>(
            hook.target_,
            callback_method,
            hook.callback_,
            phase,
            priority,
            owner);
        if (result != KEEL_RESULT_OK)
        {
            static_cast<void>(hook.target_.Reset());
            return result;
        }
        output = std::move(hook);
        return KEEL_RESULT_OK;
    }

private:
    KeelResult AddCallback(
        const Target& target,
        const KeelHookCallbackSpec& spec,
        Callback& output,
        std::shared_ptr<void> binding) const noexcept
    {
        if (!*this || !target || target.state_->api != api_ ||
            target.state_->context != context_)
        {
            return KEEL_RESULT_INVALID_ARGUMENT;
        }
        if (output.target_ || output.handle_)
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
            std::scoped_lock lock(context_->keelhook_targets_mutex);
            ++target.state_->references;
            output.Adopt(
                target.state_,
                handle,
                std::move(binding));
        }
        return result;
    }

    KeelResult ResolveTarget(
        const TargetSpec& spec,
        const KeelHookPrototype& prototype,
        Target& output)
    {
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        if (output.state_)
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        auto fresh = std::make_shared<detail::TargetState>();
        std::scoped_lock lock(context_->keelhook_targets_mutex);
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
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
            AdoptResolvedTargetLocked(handle, std::move(fresh), output);
        }
        catch (...)
        {
            static_cast<void>(api_->release_target(context_->plugin, handle));
            throw;
        }
        return KEEL_RESULT_OK;
    }

    KeelResult ResolveVirtualTarget(
        const VirtualTargetSpec& spec,
        const KeelHookPrototype& prototype,
        Target& output)
    {
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        if (output.state_)
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        if (!api_->resolve_virtual_target)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        auto fresh = std::make_shared<detail::TargetState>();
        std::scoped_lock lock(context_->keelhook_targets_mutex);
        if (!*this)
        {
            return KEEL_RESULT_NOT_READY;
        }
        KeelHookTargetHandle handle{};
        const KeelResult result = api_->resolve_virtual_target(
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
            AdoptResolvedTargetLocked(handle, std::move(fresh), output);
        }
        catch (...)
        {
            static_cast<void>(api_->release_target(context_->plugin, handle));
            throw;
        }
        return KEEL_RESULT_OK;
    }

    void AdoptResolvedTargetLocked(
        KeelHookTargetHandle handle,
        std::shared_ptr<detail::TargetState> fresh,
        Target& output)
    {
        std::shared_ptr<detail::TargetState> state;
        const auto iterator = context_->keelhook_targets.find(handle);
        if (iterator != context_->keelhook_targets.end())
        {
            state = std::static_pointer_cast<detail::TargetState>(
                iterator->second.lock());
        }
        if (!state)
        {
            fresh->context = context_;
            fresh->api = api_;
            fresh->handle = handle;
            state = std::move(fresh);
            context_->keelhook_targets[handle] = state;
        }
        ++state->references;
        output.state_ = std::move(state);
    }

    std::shared_ptr<keels2::detail::ContextState> context_;
    const KeelHookApi* api_{};
};

}

#endif
